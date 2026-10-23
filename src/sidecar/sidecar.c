/* SPDX-License-Identifier: GPL-2.0-or-later
 * bbmod sidecar (spike): the mod host loaded from OUTSIDE bb-probe.exe.
 *
 * Today the host is linked into bb-probe.exe by a patch, so every bbport release needs its own build.
 * This DLL is installed as bin/dxgi.dll next to an UNMODIFIED bb-probe.exe, which imports
 * CreateDXGIFactory1 from it (dxgi.dll is not a Windows "known DLL", so the application folder is searched
 * first). It forwards that call to the real dxgi.dll, and on a worker thread
 *   1. finds the guest game image in memory,
 *   2. creates the mod host over it (modhost.h), and
 *   3. hooks SDL_GetGamepadButton in bb-probe's import table, which the game's pad poll calls every
 *      frame from its own thread: that is the host's sync point (modhost_frame).
 * Only exported, stable entry points are used (dxgi, SDL3), nothing from bb-probe's internals.
 *
 * Spike scope: loads and runs mods, logs to bbmods-sidecar.log. No overlay or input capture yet. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "modhost.h"

static modhost *host;
static FILE *log_file;
static CRITICAL_SECTION log_cs;
static DWORD sync_thread;

/* ---- logging ------------------------------------------------------------- */

static void logf_(const char *fmt, ...) {
    char msg[1200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    SYSTEMTIME st;
    GetLocalTime(&st);
    EnterCriticalSection(&log_cs);
    if (log_file) {
        fprintf(log_file, "%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        fflush(log_file);
    }
    LeaveCriticalSection(&log_cs);
}
#define LOG(...) logf_(__VA_ARGS__)

static void host_log(void *user, const char *mod_id, int level, const char *msg) {
    static const char *names[] = {"debug", "info", "warn", "error"};
    (void)user;
    logf_("[%s] %s%s%s", names[level & 3], mod_id ? mod_id : "host", mod_id ? ": " : " ", msg);
}

/* ---- dxgi.dll proxy ------------------------------------------------------- */

typedef HRESULT(WINAPI *CreateDXGIFactory1_t)(const void *, void **);
static CreateDXGIFactory1_t real_CreateDXGIFactory1;

__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory1(const void *riid, void **factory) {
    if (!real_CreateDXGIFactory1) {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);
        wcscat(path, L"\\dxgi.dll");
        HMODULE real = LoadLibraryW(path);
        if (real) real_CreateDXGIFactory1 = (CreateDXGIFactory1_t)GetProcAddress(real, "CreateDXGIFactory1");
    }
    return real_CreateDXGIFactory1 ? real_CreateDXGIFactory1(riid, factory) : E_FAIL;
}

/* ---- finding the guest image ---------------------------------------------- */

/* The runtime reserves one block of about 89 MiB (the game's eboot image) with VirtualAlloc and later
 * gives its code pages execute rights. Nothing else in the process is of that size and shape. */
#define EBOOT_MIN 0x5500000u
#define EBOOT_MAX 0x6800000u

static int find_image(uintptr_t *base, size_t *size) {
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0x10000;
    uintptr_t cur_base = 0;
    size_t total = 0, exec = 0;
    int found = 0;
    for (;;) {
        SIZE_T got = VirtualQuery((void *)addr, &mbi, sizeof mbi);
        int more = got != 0;
        uintptr_t alloc = more ? (uintptr_t)mbi.AllocationBase : 0;
        if (!more || alloc != cur_base) {
            if (cur_base && total >= EBOOT_MIN && total <= EBOOT_MAX && exec >= 0x1000000) {
                *base = cur_base;
                *size = total;
                found++;
            }
            cur_base = alloc;
            total = exec = 0;
        }
        if (!more) break;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE) {
            total += mbi.RegionSize;
            if (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE)) exec += mbi.RegionSize;
        } else if (mbi.State != MEM_FREE) {
            cur_base = 0; /* not one contiguous private block */
        }
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (addr >= 0x7fffffff0000ull) break;
    }
    return found == 1;
}

/* ---- the sync point: an import-table hook on SDL_GetGamepadButton --------- */

typedef BOOL(__cdecl *GetGamepadButton_t)(void *, int);
static GetGamepadButton_t real_GetGamepadButton;
static volatile LONG frames;
static DWORD last_tick;

static BOOL __cdecl hook_GetGamepadButton(void *pad, int button) {
    DWORD tid = GetCurrentThreadId();
    if (host) {
        if (!sync_thread) sync_thread = tid;
        DWORD now = GetTickCount();
        if (tid == sync_thread && now - last_tick >= 4) { /* once per poll round, not per button */
            last_tick = now;
            modhost_frame(host);
            frames++;
        }
    }
    return real_GetGamepadButton(pad, button);
}

static int hook_import(HMODULE module, const char *dll, const char *func, void *replacement, void **original) {
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp((const char *)(base + d->Name), dll)) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA *thunks = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, thunks++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *n = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)n->Name, func)) continue;
            DWORD old;
            if (!VirtualProtect(&thunks->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) return 0;
            *original = (void *)thunks->u1.Function;
            thunks->u1.Function = (ULONG_PTR)replacement;
            VirtualProtect(&thunks->u1.Function, sizeof(void *), old, &old);
            return 1;
        }
    }
    return 0;
}

/* ---- host wiring ---------------------------------------------------------- */

static int call_guarded(void *user, void (*fn)(void *), void *arg) {
    (void)user;
    __try { fn(arg); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
    return 0;
}

typedef const uint8_t *(__cdecl *GetKeyboardState_t)(int *);
static GetKeyboardState_t sdl_keyboard;
static const uint8_t *keyboard(void *user, int *count) {
    (void)user;
    return sdl_keyboard ? sdl_keyboard(count) : NULL;
}

static DWORD WINAPI init_thread(void *unused) {
    (void)unused;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) *slash = 0; /* bin folder */
    char bin[MAX_PATH * 3], port[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, exe, -1, bin, sizeof bin, NULL, NULL);
    snprintf(port, sizeof port, "%s\\..", bin);

    char logp[MAX_PATH * 3 + 40];
    snprintf(logp, sizeof logp, "%s\\bbmods-sidecar.log", port);
    log_file = fopen(logp, "w");
    LOG("bbmod sidecar starting (pid %lu)", GetCurrentProcessId());

    /* The mods read BB_CODEMODS_DIR (settings, the Mod Manager's installs). */
    static char mods[MAX_PATH * 3], cache[MAX_PATH * 3 + 8];
    const char *env = getenv("BB_CODEMODS_DIR");
    if (env && *env) snprintf(mods, sizeof mods, "%s", env);
    else {
        char full[MAX_PATH];
        snprintf(mods, sizeof mods, "%s\\..\\bbmods", bin);
        if (GetFullPathNameA(mods, MAX_PATH, full, NULL)) snprintf(mods, sizeof mods, "%s", full);
        SetEnvironmentVariableA("BB_CODEMODS_DIR", mods);
    }
    snprintf(cache, sizeof cache, "%s-cache", mods);
    CreateDirectoryA(mods, NULL);

    /* Wait for the runtime to map the game. */
    uintptr_t base = 0;
    size_t size = 0;
    for (int i = 0; i < 1200 && !find_image(&base, &size); i++) Sleep(100);
    if (!base) { LOG("could not find the game image; giving up"); return 0; }
    LOG("game image at %p, %zu bytes", (void *)base, size);

    HMODULE sdl = GetModuleHandleA("SDL3.dll");
    if (sdl) sdl_keyboard = (GetKeyboardState_t)GetProcAddress(sdl, "SDL_GetKeyboardState");

    modhost_config c;
    memset(&c, 0, sizeof c);
    c.mods_dir = mods;
    c.cache_dir = cache;
    c.eboot_base = base;
    c.eboot_size = size;
    c.ps4_eboot_base = 0x400000;
    c.log = host_log;
    c.platform.call_guarded = call_guarded;
    c.input.keyboard = keyboard;
    host = modhost_create(&c);
    if (!host) { LOG("modhost_create failed"); return 0; }
    int n = modhost_scan(host);
    LOG("mod host ready: %d mod(s) found in %s", n, mods);

    if (hook_import(GetModuleHandleW(NULL), "SDL3.dll", "SDL_GetGamepadButton", (void *)hook_GetGamepadButton, (void **)&real_GetGamepadButton))
        LOG("sync point hooked: SDL_GetGamepadButton");
    else
        LOG("bb-probe does not import SDL_GetGamepadButton; mods will not get frame callbacks");

    for (int i = 0; i < 40; i++) { Sleep(15000); LOG("alive: %ld frame(s) so far", (long)frames); }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&log_cs);
        /* Only inside bb-probe: other processes that load a dxgi.dll from here get just the forwarder. */
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        wchar_t *name = wcsrchr(exe, L'\\');
        if (name && !_wcsicmp(name + 1, L"bb-probe.exe")) CreateThread(NULL, 0, init_thread, NULL, 0, NULL);
    }
    return TRUE;
}
