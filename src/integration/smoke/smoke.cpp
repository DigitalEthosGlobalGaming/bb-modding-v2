// Windows smoke test of the bbport integration, run without the game:
// bbport's own src/runtime_modhost.c, gpu/shim/bbport_modui.cpp and bb_setjmp/bb_longjmp
// (src/compat_win.c), a copy of probe.c's fault-handler recovery, real Dear ImGui drawing the
// Mods window, and real mod DLLs (a healthy one and one that crashes in a frame callback).
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "runtime.h"
#include "runtime_modhost.h"
#include "modhost.h"
}
#include "bbport_modui.h"
#include "imgui.h"
#include "imgui_internal.h"

extern "C" {
__thread sigjmp_buf* runtime_fault_recover;
int bbgpu_overlay_captures_input(void) { return 0; }
static bool keys[512];
const bool* SDL_GetKeyboardState(int* n) { if (n) *n = 512; return keys; }
uint32_t SDL_WasInit(uint32_t) { return 1; }
modhost* runtime_modhost_get(void);
int runtime_pad_effect_info(void*, bbm_pad_info* out) { std::memset(out, 0, sizeof *out); return 0; }
int runtime_pad_effect_send(void*, const uint8_t*, const uint8_t*) { return 0; }
int runtime_pad_effect_led(void*, uint8_t, uint8_t, uint8_t) { return 0; }
}

static int fails;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

// probe.c vectored_fault, the parts mods rely on.
static LONG CALLBACK vectored_fault(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    CONTEXT* c = info->ContextRecord;
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && runtime_fault_recover) {
        unsigned long long* buffer = *runtime_fault_recover;
        runtime_fault_recover = NULL;
        c->Rsp = ((c->Rsp - 128) & ~(DWORD64)15) - 40;
        c->Rcx = (DWORD64)(uintptr_t)buffer;
        c->Rdx = 1;
        c->Rip = (DWORD64)(uintptr_t)bb_longjmp;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (const char* mod = runtime_modhost_fault_owner((uintptr_t)c->Rip)) {
        std::fprintf(stderr, "Fault inside code mod '%s' (hook)\n", mod);
        ExitProcess(3);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void write_file(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}
static void copy_file(const char* from, const std::string& to) {
    CHECK(CopyFileA(from, to.c_str(), FALSE));
}
static modhost_state state_of(modhost* h, const char* id) {
    for (size_t i = 0; i < modhost_mod_count(h); i++) {
        modhost_mod_view v;
        modhost_mod_view_at(h, i, &v);
        if (!std::strcmp(v.id, id)) return v.state;
    }
    return (modhost_state)-1;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: smoke <test_mod.dll> <test_crash_mod.dll>\n"); return 2; }
    AddVectoredExceptionHandler(1, vectored_fault);

    char tmp[MAX_PATH];
    GetTempPathA(sizeof tmp, tmp);
    std::string root = std::string(tmp) + "bbmod-smoke-" + std::to_string(GetTickCount());
    std::string mods = root + "\\bbmods";
    CreateDirectoryA(root.c_str(), NULL);
    CreateDirectoryA(mods.c_str(), NULL);
    CreateDirectoryA((mods + "\\healthy").c_str(), NULL);
    CreateDirectoryA((mods + "\\crash").c_str(), NULL);
    copy_file(argv[1], mods + "\\healthy\\m.dll");
    write_file(mods + "\\healthy\\mod.toml", "[mod]\nid = \"test.mod\"\nname = \"Healthy\"\ndll = \"m.dll\"\n");
    copy_file(argv[2], mods + "\\crash\\m.dll");
    write_file(mods + "\\crash\\mod.toml", "[mod]\nid = \"crash\"\nname = \"Crashy\"\ndll = \"m.dll\"\n[test]\ncrash = \"frame\"\n");
    _putenv_s("BB_CODEMODS_DIR", mods.c_str());

    unsigned char* image = (unsigned char*)VirtualAlloc(NULL, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    image[0x10] = 0x11; image[0x20] = 0x22;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 720);
    io.IniFilename = nullptr;
    io.Fonts->Build();

    runtime_modhost_start(image, 65536);
    modhost* h = runtime_modhost_get();
    CHECK(h != nullptr);
    CHECK(state_of(h, "test.mod") == MODHOST_ACTIVE && state_of(h, "crash") == MODHOST_ACTIVE);
    CHECK(image[0x10] == 0x90 && image[0x20] == 0x90);

    bool drew_mods = false, drew_panel = false, toast = false, drew_log = false;
    for (int frame = 0; frame < 8; frame++) {
        keys[62] = (frame == 2);                                   // F5 for the healthy mod
        runtime_modhost_pad(frame == 4 ? BBM_PAD_TRIANGLE : 0);   // what the game just read
        runtime_modhost_frame();                                   // the crash mod faults on frame 3
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        BbModUi::Draw(true);
        drew_mods |= ImGui::FindWindowByName("Mods") != nullptr;
        drew_panel |= ImGui::FindWindowByName("Test##test.mod") != nullptr;
        drew_log |= ImGui::FindWindowByName("Mod log") != nullptr;
        toast |= BbModUi::Visible();
        ImGui::Render();
    }
    CHECK(state_of(h, "crash") == MODHOST_FAULTED);   // recovered through bb_longjmp
    CHECK(image[0x20] == 0x22);                      // its patch reverted
    CHECK(state_of(h, "test.mod") == MODHOST_ACTIVE); // the other mod untouched
    CHECK(image[0x10] == 0x90);
    CHECK(drew_mods && drew_panel && drew_log);

    // The log file: every line, flushed as it is written (readable while the game runs).
    const char* log_path = runtime_modhost_log_path();
    CHECK(log_path && *log_path);
    std::string log_text;
    if (FILE* lf = std::fopen(log_path, "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, lf)) > 0) log_text.append(buf, n);
        std::fclose(lf);
    }
    CHECK(log_text.find("bbport code mods log, started") != std::string::npos);
    CHECK(log_text.find("[info] test.mod: loaded Healthy") != std::string::npos);
    CHECK(log_text.find("[error] crash: crashed in a callback") != std::string::npos);
    std::printf("smoke: log file %s (%zu bytes)\n", log_path, log_text.size());
    CHECK(toast);                                    // the crash was announced

    char out[1024];
    CHECK(runtime_modhost_resolve("/app0/dvdroot_ps4/test/a.bin", out, sizeof out) == 1);
    CHECK(runtime_modhost_resolve("/app0/dvdroot_ps4/param/other.bin", out, sizeof out) == 0);

    std::printf("smoke: crash recovered via bb_longjmp, Mods window drawn, overrides resolved\n");
    if (fails) return 1;
    std::puts("integration smoke test passed");
    return 0;
}
