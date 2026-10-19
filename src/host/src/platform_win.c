/* SPDX-License-Identifier: GPL-2.0-or-later
 * Windows platform layer. bbport on Windows should still pass its own
 * write_code hook if GPU write tracking needs to see code-page changes, and
 * its own suspend_world if it keeps a list of guest threads; the defaults
 * below suspend every other thread in the process. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include "internal.h"

uint64_t plat_now_ms(void) { return GetTickCount64(); }

double plat_now_s(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

void plat_sleep_ms(uint32_t ms) { Sleep(ms); }

static int to_wide(const char *s, wchar_t *out, int cap) {
    return MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap) > 0 ? 0 : -1;
}

int plat_stat(const char *path, uint64_t *mtime, uint64_t *size, int *is_dir) {
    wchar_t w[MH_PATH];
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (to_wide(path, w, MH_PATH) || !GetFileAttributesExW(w, GetFileExInfoStandard, &a)) return -1;
    if (mtime) *mtime = ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
    if (size) *size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    if (is_dir) *is_dir = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return 0;
}

int plat_copy_file(const char *src, const char *dst) {
    wchar_t a[MH_PATH], b[MH_PATH];
    if (to_wide(src, a, MH_PATH) || to_wide(dst, b, MH_PATH)) return -1;
    return CopyFileW(a, b, FALSE) ? 0 : -1;
}

int plat_mkdirs(const char *path) {
    char tmp[MH_PATH];
    mh_strlcpy(tmp, path, sizeof tmp);
    /* Skip a drive prefix ("C:\\") so it is not "created"; otherwise start at 1. */
    for (char *p = tmp + ((tmp[0] && tmp[1] == ':') ? 3 : 1); *p; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = 0;
            CreateDirectoryA(tmp, NULL);
            *p = c;
        }
    }
    if (!CreateDirectoryA(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return -1;
    return 0;
}

int plat_remove(const char *path) {
    wchar_t w[MH_PATH];
    if (to_wide(path, w, MH_PATH)) return -1;
    return DeleteFileW(w) ? 0 : -1;
}

int plat_list_dir(const char *path, plat_dir_cb cb, void *user) {
    char pattern[MH_PATH];
    wchar_t w[MH_PATH];
    mh_path_join(pattern, sizeof pattern, path, "*");
    if (to_wide(pattern, w, MH_PATH)) return -1;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        char name[MH_PATH];
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name, NULL, NULL);
        if (name[0] == '.') continue;
        cb(user, name, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

void *plat_lib_open(const char *path, char *err, size_t cap) {
    wchar_t w[MH_PATH], full[MH_PATH];
    if (to_wide(path, w, MH_PATH)) return NULL;
    /* LOAD_WITH_ALTERED_SEARCH_PATH needs a fully qualified path; the cache
     * directory may have been given relative to the game folder. */
    DWORD n = GetFullPathNameW(w, MH_PATH, full, NULL);
    if (n == 0 || n >= MH_PATH) return NULL;
    HMODULE m = LoadLibraryExW(full, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m && err) snprintf(err, cap, "LoadLibrary failed (error %lu)", GetLastError());
    return m;
}

void *plat_lib_sym(void *lib, const char *name) { return (void *)GetProcAddress((HMODULE)lib, name); }
void plat_lib_close(void *lib) { FreeLibrary((HMODULE)lib); }

int plat_lib_range(void *lib, void *sym, uintptr_t *lo, uintptr_t *hi) {
    (void)sym;
    const uint8_t *base = (const uint8_t *)lib;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return -1;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    *lo = (uintptr_t)base;
    *hi = (uintptr_t)base + nt->OptionalHeader.SizeOfImage;
    return 0;
}

void *plat_alloc_exec_near(uintptr_t near_addr, size_t size) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (!near_addr) return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    const uintptr_t gran = si.dwAllocationGranularity, span = 0x70000000;
    uintptr_t base = near_addr & ~(gran - 1);
    for (uintptr_t off = gran; off < span; off += gran) {
        for (int dir = 0; dir < 2; dir++) {
            if (!dir && base < off) continue;
            uintptr_t want = dir ? base + off : base - off;
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery((void *)want, &mbi, sizeof mbi) || mbi.State != MEM_FREE) continue;
            void *p = VirtualAlloc((void *)want, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p) return p;
        }
    }
    return NULL;
}

void plat_free_exec(void *p, size_t size) { (void)size; VirtualFree(p, 0, MEM_RELEASE); }

int plat_write_code(void *addr, const void *src, size_t n) {
    /* Guest heaps can be non-executable mapped sections. They already permit
     * writes, but Windows rejects upgrading the view to executable protection.
     * Preserve their protection and use the fault-safe OS write instead. */
    MEMORY_BASIC_INFORMATION mbi;
    if (n && VirtualQuery(addr, &mbi, sizeof mbi) == sizeof mbi &&
        mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        ((mbi.Protect & 0xff) == PAGE_READWRITE || (mbi.Protect & 0xff) == PAGE_WRITECOPY) &&
        (uintptr_t)addr >= (uintptr_t)mbi.BaseAddress &&
        n <= mbi.RegionSize - ((uintptr_t)addr - (uintptr_t)mbi.BaseAddress)) {
        SIZE_T written = 0;
        return WriteProcessMemory(GetCurrentProcess(), addr, src, n, &written) && written == n ? 0 : -1;
    }
    DWORD old;
    if (!VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old)) return -1;
    memcpy(addr, src, n);
    VirtualProtect(addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), addr, n);
    return 0;
}

int plat_read_safe(const void *addr, void *out, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), addr, out, n, &got) && got == n ? 0 : -1;
}

int plat_next_region(uintptr_t addr, uintptr_t *base, size_t *size, uint32_t *flags) {
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t p = addr;
    while (VirtualQuery((const void *)p, &mbi, sizeof mbi) == sizeof mbi) {
        uintptr_t lo = (uintptr_t)mbi.BaseAddress, hi = lo + mbi.RegionSize;
        if (hi <= p) break; /* no progress: end of the address space */
        DWORD prot = mbi.Protect & 0xFF;
        int readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                       prot != PAGE_NOACCESS && prot != PAGE_EXECUTE;
        if (readable && hi > addr) {
            int write = prot == PAGE_READWRITE || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READWRITE ||
                        prot == PAGE_EXECUTE_WRITECOPY;
            int exec = prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
            *base = lo;
            *size = mbi.RegionSize;
            *flags = (write ? BBM_REGION_WRITE : 0) | (exec ? BBM_REGION_EXEC : 0) |
                     (mbi.Type == MEM_IMAGE ? BBM_REGION_IMAGE : 0);
            return 0;
        }
        p = hi;
    }
    return -1;
}

/* Suspended threads. Handles are collected BEFORE suspending so nothing
 * allocates while another thread may hold the heap lock. */
static HANDLE g_threads[1024];
static int g_nthreads;

int plat_suspend_world(void) {
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;
    THREADENTRY32 te = {.dwSize = sizeof te};
    g_nthreads = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            if (g_nthreads >= 1024) break;
            HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
            if (t) g_threads[g_nthreads++] = t;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    for (int i = 0; i < g_nthreads; i++) SuspendThread(g_threads[i]);
    return 0;
}

void plat_resume_world(void) {
    for (int i = 0; i < g_nthreads; i++) {
        ResumeThread(g_threads[i]);
        CloseHandle(g_threads[i]);
    }
    g_nthreads = 0;
}

int plat_any_thread_in(uintptr_t lo, uintptr_t hi) {
    for (int i = 0; i < g_nthreads; i++) {
        CONTEXT c = {.ContextFlags = CONTEXT_CONTROL};
        if (GetThreadContext(g_threads[i], &c) && c.Rip >= lo && c.Rip < hi) return 1;
    }
    return 0;
}

/* CRITICAL_SECTION: recursive and try-lockable, which the run lock needs. */
struct plat_mutex { CRITICAL_SECTION cs; };
plat_mutex *plat_mutex_new(void) {
    plat_mutex *m = calloc(1, sizeof *m);
    InitializeCriticalSection(&m->cs);
    return m;
}
plat_mutex *plat_mutex_new_recursive(void) { return plat_mutex_new(); }
void plat_mutex_free(plat_mutex *m) { if (m) { DeleteCriticalSection(&m->cs); free(m); } }
int plat_mutex_trylock(plat_mutex *m) { return TryEnterCriticalSection(&m->cs) != 0; }
void plat_mutex_lock(plat_mutex *m) { EnterCriticalSection(&m->cs); }
void plat_mutex_unlock(plat_mutex *m) { LeaveCriticalSection(&m->cs); }

struct thread_start { void (*fn)(void *); void *arg; };
static DWORD WINAPI thread_tramp(LPVOID p) {
    struct thread_start s = *(struct thread_start *)p;
    free(p);
    s.fn(s.arg);
    return 0;
}

void *plat_thread_start(void (*fn)(void *), void *arg) {
    struct thread_start *s = malloc(sizeof *s);
    s->fn = fn;
    s->arg = arg;
    HANDLE t = CreateThread(NULL, 0, thread_tramp, s, 0, NULL);
    if (!t) free(s);
    return t;
}

void plat_thread_join(void *t) {
    if (!t) return;
    WaitForSingleObject((HANDLE)t, INFINITE);
    CloseHandle((HANDLE)t);
}

int plat_thread_join_timeout(void *t, uint32_t ms) {
    if (!t) return 0;
    if (WaitForSingleObject((HANDLE)t, ms) != WAIT_OBJECT_0) return -1; /* still running: leak */
    CloseHandle((HANDLE)t);
    return 0;
}
#endif
