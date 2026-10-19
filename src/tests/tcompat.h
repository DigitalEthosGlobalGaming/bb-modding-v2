/* Tiny portability layer so the same tests run on Linux and on Windows. */
#ifndef TCOMPAT_H
#define TCOMPAT_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HANDLE tc_thread;
static void *tc_alloc_rwx(size_t n) { return VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); }
static void tc_free_rwx(void *p, size_t n) { (void)n; VirtualFree(p, 0, MEM_RELEASE); }
static void tc_yield(void) { SwitchToThread(); }
static void tc_sleep_us(unsigned us) { Sleep(us < 1000 ? 1 : us / 1000); }
static int tc_mkdir1(const char *p) { return CreateDirectoryA(p, NULL) || GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1; }
static tc_thread tc_thread_start(void *(*fn)(void *), void *arg) {
    return CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(void *)fn, arg, 0, NULL);
}
static void tc_thread_join(tc_thread t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
static void tc_tmpdir(char *out, size_t cap, const char *tag) {
    char base[260];
    GetTempPathA(sizeof base, base);
    snprintf(out, cap, "%s%s-%lu-%lu", base, tag, (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
    for (char *p = out; *p; p++) if (*p == '\\') *p = '/';
}
static void tc_rmtree(const char *dir) {
    char pat[600];
    snprintf(pat, sizeof pat, "%s/*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            char sub[900];
            snprintf(sub, sizeof sub, "%s/%s", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) tc_rmtree(sub);
            else DeleteFileA(sub);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir);
}
#else
#define _GNU_SOURCE
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
typedef pthread_t tc_thread;
static void *tc_alloc_rwx(size_t n) {
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
static void tc_free_rwx(void *p, size_t n) { munmap(p, n); }
static void tc_yield(void) { sched_yield(); }
static void tc_sleep_us(unsigned us) { usleep(us); }
static int tc_mkdir1(const char *p) { return mkdir(p, 0755) == 0 || access(p, F_OK) == 0 ? 0 : -1; }
static tc_thread tc_thread_start(void *(*fn)(void *), void *arg) {
    pthread_t t;
    pthread_create(&t, NULL, fn, arg);
    return t;
}
static void tc_thread_join(tc_thread t) { pthread_join(t, NULL); }
static void tc_tmpdir(char *out, size_t cap, const char *tag) {
    snprintf(out, cap, "/tmp/%s-XXXXXX", tag);
    if (!mkdtemp(out)) out[0] = 0;
}
static void tc_rmtree(const char *dir) {
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char sub[900];
            snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
            struct stat st;
            if (lstat(sub, &st) == 0 && S_ISDIR(st.st_mode)) tc_rmtree(sub);
            else unlink(sub);
        }
        closedir(d);
    }
    rmdir(dir);
}
#endif

/* mkdir -p */
static void tc_mkdirs(const char *path) {
    char tmp[900];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; if (!(p - tmp == 2 && tmp[1] == ':')) tc_mkdir1(tmp); *p = '/'; }
    tc_mkdir1(tmp);
}
static int tc_write(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(text, 1, strlen(text), f);
    return fclose(f);
}
static int tc_copy(const char *src, const char *dst) {
    FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
    if (!a || !b) { if (a) fclose(a); if (b) fclose(b); return -1; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
    fclose(a);
    return fclose(b);
}
/* Creates <mods>/<name>/{m.dll,mod.toml}. extra is appended to mod.toml. */
static void tc_make_mod(const char *mods, const char *name, const char *dll_src, const char *toml) {
    char d[900], p[1000];
    snprintf(d, sizeof d, "%s/%s", mods, name);
    tc_mkdirs(d);
    snprintf(p, sizeof p, "%s/m.dll", d);
    if (tc_copy(dll_src, p)) { fprintf(stderr, "cannot copy %s\n", dll_src); exit(2); }
    snprintf(p, sizeof p, "%s/mod.toml", d);
    if (tc_write(p, toml)) exit(2);
}
#endif
