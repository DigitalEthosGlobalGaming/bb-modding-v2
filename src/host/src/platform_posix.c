/* SPDX-License-Identifier: GPL-2.0-or-later
 * POSIX platform layer. Used by the Linux build of bbport and by the tests.
 * Thread suspension is a no-op here: on Linux bbport should pass its own
 * suspend_world/any_thread_in hooks (it already stops guest threads for
 * diagnostics). */
#ifndef _WIN32
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "internal.h"

uint64_t plat_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

double plat_now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void plat_sleep_ms(uint32_t ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

int plat_stat(const char *path, uint64_t *mtime, uint64_t *size, int *is_dir) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (mtime) *mtime = (uint64_t)st.st_mtim.tv_sec * 1000000000ull + (uint64_t)st.st_mtim.tv_nsec;
    if (size) *size = (uint64_t)st.st_size;
    if (is_dir) *is_dir = S_ISDIR(st.st_mode);
    return 0;
}

int plat_copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (out < 0) { close(in); return -1; }
    char buf[65536];
    ssize_t r;
    int rc = 0;
    while ((r = read(in, buf, sizeof buf)) > 0) {
        if (write(out, buf, (size_t)r) != r) { rc = -1; break; }
    }
    if (r < 0) rc = -1;
    close(in);
    close(out);
    return rc;
}

int plat_mkdirs(const char *path) {
    char tmp[MH_PATH];
    mh_strlcpy(tmp, path, sizeof tmp);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

int plat_remove(const char *path) { return unlink(path); }

int plat_list_dir(const char *path, plat_dir_cb cb, void *user) {
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char full[MH_PATH];
        mh_path_join(full, sizeof full, path, e->d_name);
        int is_dir = 0;
        plat_stat(full, NULL, NULL, &is_dir);
        cb(user, e->d_name, is_dir);
    }
    closedir(d);
    return 0;
}

void *plat_lib_open(const char *path, char *err, size_t cap) {
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h && err) mh_strlcpy(err, dlerror(), cap);
    return h;
}

void *plat_lib_sym(void *lib, const char *name) { return dlsym(lib, name); }
void plat_lib_close(void *lib) { dlclose(lib); }

struct range_q { uintptr_t sym, lo, hi; int found; };

static int phdr_cb(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    struct range_q *q = data;
    uintptr_t lo = UINTPTR_MAX, hi = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD) continue;
        uintptr_t a = info->dlpi_addr + ph->p_vaddr;
        if (a < lo) lo = a;
        if (a + ph->p_memsz > hi) hi = a + ph->p_memsz;
    }
    if (q->sym >= lo && q->sym < hi) { q->lo = lo; q->hi = hi; q->found = 1; return 1; }
    return 0;
}

int plat_lib_range(void *lib, void *sym, uintptr_t *lo, uintptr_t *hi) {
    (void)lib;
    struct range_q q = {(uintptr_t)sym, 0, 0, 0};
    dl_iterate_phdr(phdr_cb, &q);
    if (!q.found) return -1;
    *lo = q.lo;
    *hi = q.hi;
    return 0;
}

void *plat_alloc_exec_near(uintptr_t near_addr, size_t size) {
    long page = sysconf(_SC_PAGESIZE);
    size = (size + (size_t)page - 1) & ~((size_t)page - 1);
    if (!near_addr) {
        void *p = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return p == MAP_FAILED ? NULL : p;
    }
    const uintptr_t step = 0x10000, span = 0x70000000; /* stay well inside +-2 GB */
    uintptr_t base = near_addr & ~(step - 1);
    for (uintptr_t off = step; off < span; off += step) {
        for (int dir = 0; dir < 2; dir++) {
            uintptr_t want = dir ? base + off : base - off;
            if (!dir && base < off) continue;
#ifdef MAP_FIXED_NOREPLACE
            void *p = mmap((void *)want, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
#else
            void *p = mmap((void *)want, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
            if (p == MAP_FAILED) continue;
            intptr_t d = (intptr_t)((uintptr_t)p - near_addr);
            if (d > -(intptr_t)span && d < (intptr_t)span) return p;
            munmap(p, size);
        }
    }
    return NULL;
}

void plat_free_exec(void *p, size_t size) {
    long page = sysconf(_SC_PAGESIZE);
    size = (size + (size_t)page - 1) & ~((size_t)page - 1);
    munmap(p, size);
}

/* Current protection of the page holding addr, from /proc/self/maps. */
static int page_prot(uintptr_t addr) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return PROT_READ | PROT_WRITE | PROT_EXEC;
    char line[512];
    int prot = PROT_READ | PROT_EXEC;
    while (fgets(line, sizeof line, f)) {
        unsigned long lo, hi;
        char perms[8];
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) != 3) continue;
        if (addr >= lo && addr < hi) {
            prot = (perms[0] == 'r' ? PROT_READ : 0) | (perms[1] == 'w' ? PROT_WRITE : 0) |
                   (perms[2] == 'x' ? PROT_EXEC : 0);
            break;
        }
    }
    fclose(f);
    return prot;
}

int plat_write_code(void *addr, const void *src, size_t n) {
    long page = sysconf(_SC_PAGESIZE);
    uintptr_t lo = (uintptr_t)addr & ~((uintptr_t)page - 1);
    uintptr_t hi = ((uintptr_t)addr + n + (uintptr_t)page - 1) & ~((uintptr_t)page - 1);
    int prot = page_prot((uintptr_t)addr);
    if (!(prot & PROT_WRITE)) {
        if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return -1;
    }
    memcpy(addr, src, n);
    if (!(prot & PROT_WRITE)) mprotect((void *)lo, hi - lo, prot);
    __builtin___clear_cache((char *)addr, (char *)addr + n);
    return 0;
}

int plat_read_safe(const void *addr, void *out, size_t n) {
    struct iovec local = {out, n}, remote = {(void *)addr, n};
    ssize_t r = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    if (r == (ssize_t)n) return 0;
    if (r < 0 && errno == ENOSYS) { memcpy(out, addr, n); return 0; }
    return -1;
}

/* /proc/self/maps, re-read per call: a scanner calls it once per region. */
int plat_next_region(uintptr_t addr, uintptr_t *base, size_t *size, uint32_t *flags) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return -1;
    char line[512];
    int found = -1;
    while (fgets(line, sizeof line, f)) {
        unsigned long lo, hi;
        char perms[8] = "";
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) != 3) continue;
        if (hi <= addr || perms[0] != 'r') continue;
        if (strstr(line, "[vvar]") || strstr(line, "[vsyscall]")) continue; /* not plainly readable */
        *base = (uintptr_t)lo;
        *size = (size_t)(hi - lo);
        *flags = (perms[1] == 'w' ? BBM_REGION_WRITE : 0) | (perms[2] == 'x' ? BBM_REGION_EXEC : 0) |
                 (strchr(line, '/') ? BBM_REGION_IMAGE : 0);
        found = 0;
        break;
    }
    fclose(f);
    return found;
}

int plat_suspend_world(void) { return 0; }
void plat_resume_world(void) {}
int plat_any_thread_in(uintptr_t lo, uintptr_t hi) { (void)lo; (void)hi; return 0; }

struct plat_mutex { pthread_mutex_t m; };

plat_mutex *plat_mutex_new(void) {
    plat_mutex *m = calloc(1, sizeof *m);
    pthread_mutex_init(&m->m, NULL);
    return m;
}
plat_mutex *plat_mutex_new_recursive(void) {
    plat_mutex *m = calloc(1, sizeof *m);
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&m->m, &a);
    pthread_mutexattr_destroy(&a);
    return m;
}
void plat_mutex_free(plat_mutex *m) { if (m) { pthread_mutex_destroy(&m->m); free(m); } }
int plat_mutex_trylock(plat_mutex *m) { return pthread_mutex_trylock(&m->m) == 0; }
void plat_mutex_lock(plat_mutex *m) { pthread_mutex_lock(&m->m); }
void plat_mutex_unlock(plat_mutex *m) { pthread_mutex_unlock(&m->m); }

struct thread_start { void (*fn)(void *); void *arg; };
static void *thread_tramp(void *p) {
    struct thread_start s = *(struct thread_start *)p;
    free(p);
    s.fn(s.arg);
    return NULL;
}

void *plat_thread_start(void (*fn)(void *), void *arg) {
    pthread_t *t = malloc(sizeof *t);
    struct thread_start *s = malloc(sizeof *s);
    s->fn = fn;
    s->arg = arg;
    if (pthread_create(t, NULL, thread_tramp, s) != 0) { free(t); free(s); return NULL; }
    return t;
}

void plat_thread_join(void *t) {
    if (!t) return;
    pthread_join(*(pthread_t *)t, NULL);
    free(t);
}

int plat_thread_join_timeout(void *t, uint32_t ms) {
    if (!t) return 0;
#ifdef __GLIBC__
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    if (pthread_timedjoin_np(*(pthread_t *)t, NULL, &ts) != 0) return -1; /* still running: leak */
#else
    (void)ms;
    pthread_join(*(pthread_t *)t, NULL);
#endif
    free(t);
    return 0;
}
#endif
