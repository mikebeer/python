/* =========================================================================
   p0_os.c   standard headers and the operating-system adapter layer.

   EVERYTHING that touches the host system lives in this one file:
   path composition and the search-path separators, reading module and
   script files, fopen, time and clock, getenv, the working directory, the
   environment table, isatty, sleep, remove and rename.  The rest of the
   interpreter only calls the host_* functions below, so a port (VM/370
   CMS, MVS, ...) changes this file and nothing else.

   POSIX calls are compiled only with -DPY_POSIX.  The interpreter uses no
   popen, system() or directory listing, so there is no adapter for them.
   The socket adapter (host_sock_*, last section) is BSD sockets, IPv4 TCP only,
   and exists only with -DPY_POSIX; a CMS TCP/IP port replaces that section
   (see PORTING.md).  Without it every host_sock_* call fails and the socket
   module reports 'sockets not available'.
   -DPY_HOST_CMS (set automatically when __CMS__ is defined) selects the
   CMS style "fn ft fm" file names for module files (see host_module_path).
   -DPY_EMBED_LIBS compiles the pure-Python library tree into the binary
   (pylib_embed.c, made by tools/embed_pylib.py); embedded modules are
   found under the pseudo directory "<pylib>" before any file on disk.
   ========================================================================= */
#ifdef PY_POSIX
#define _XOPEN_SOURCE 600
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <time.h>
#include <limits.h>
#include <stdarg.h>
#include <float.h>

#ifdef PY_POSIX
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#endif

#ifdef __CMS__
#ifndef PY_HOST_CMS
#define PY_HOST_CMS 1
#endif
/* GCCLIB31's setjmp.h declares only jmp_buf; the routines are assembler */
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#endif

/* ---- configuration of names -------------------------------------------- */
#ifndef PY_DIRSEP
#define PY_DIRSEP "/"            /* joins a directory and a name */
#endif
#ifndef PY_PARDIR
#define PY_PARDIR ".."           /* the parent directory name */
#endif
#ifndef PY_PATHLIST_SEP
#define PY_PATHLIST_SEP ":"      /* separates entries of $PYLIB */
#endif
#ifndef PY_PATHMAX
#define PY_PATHMAX 1024          /* longest composed file name */
#endif
#ifndef PY_EXE_LIBS
#ifdef PY_HOST_CMS
#define PY_EXE_LIBS 0            /* no "<exe dir>/pylib" on CMS */
#else
#define PY_EXE_LIBS 1
#endif
#endif
#ifndef PY_RMODE
#ifdef PY_HOST_CMS
#define PY_RMODE "r"             /* CMS text records: the C library adds the newlines */
#else
#define PY_RMODE "rb"            /* open files for reading (source, getsize, ...) */
#endif
#endif
#define PY_EMBED_DIR "<pylib>"   /* pseudo directory of embedded modules */

/* ---- embedded library table (data made by tools/embed_pylib.py) --------- */
#define PYEMB_CHUNK 32000L       /* bytes per array: C89 minimum is 32767 */
typedef struct PyEmbFile {
    const char *name;                       /* "numpy/__init__.py" */
    long len;                               /* size in bytes */
    const unsigned char *const *chunks;     /* PYEMB_CHUNK bytes each */
} PyEmbFile;

#ifdef PY_EMBED_LIBS
#include "pylib_embed.c"
#endif

/* The embedded bytes are ASCII codes.  This literal set maps ASCII 32..126
   to the host character set (the compiler translates the literal), so the
   source of the embedded libraries is correct on an EBCDIC host too. */
static const char host_ascii_set[] =
    " !\"#$%&'()*+,-./0123456789:;<=>?@"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
    "abcdefghijklmnopqrstuvwxyz{|}~";
static int host_from_ascii(int a)
{
    if (a >= 32 && a <= 126) return (unsigned char)host_ascii_set[a - 32];
    if (a == 10) return '\n';
    if (a == 9) return '\t';
    if (a == 13) return '\r';
    if (a == 12) return '\f';
    if (a == 11) return '\v';
    return a;
}

static int host_is_embed_path(const char *p)
{
    size_t n = strlen(PY_EMBED_DIR);
    return strncmp(p, PY_EMBED_DIR, n) == 0 && (p[n] == 0 || p[n] == '/');
}

/* ---- paths -------------------------------------------------------------- */
/* dir + name into out (capacity PY_PATHMAX); returns the length or -1 */
static long host_join(char *out, const char *dir, long dlen, const char *name, long nlen)
{
    long n = 0, sl = (long)strlen(PY_DIRSEP);
    if (dlen > 0) {
        if (dlen + sl + nlen + 1 > PY_PATHMAX) return -1;
        memcpy(out, dir, (size_t)dlen);
        n = dlen;
        if (n < sl || memcmp(out + n - sl, PY_DIRSEP, (size_t)sl) != 0) {
            memcpy(out + n, PY_DIRSEP, (size_t)sl);
            n += sl;
        }
    } else if (nlen + 1 > PY_PATHMAX) return -1;
    memcpy(out + n, name, (size_t)nlen);
    n += nlen;
    out[n] = 0;
    return n;
}

/* length of the directory part of a file name (0 if there is none) */
static long host_dirname_len(const char *path)
{
    long i, last = -1, n = (long)strlen(path);
    for (i = 0; i < n; i++) if (path[i] == '/') last = i;
    return last < 0 ? 0 : (last == 0 ? 1 : last);
}

/* The file that holds module `leaf` found in search directory `dir`.
   ispkg: the package initialisation file of directory `leaf`, otherwise
   the module file.  Returns the length or -1.  Embedded modules always
   use '/' (the table is keyed that way); CMS names are "FN FT FM" with the
   directory entry being the file mode and packages kept flat. */
static long host_module_path(char *out, const char *dir, long dlen, const char *leaf,
                             long llen, int ispkg)
{
    long n;
    if (host_is_embed_path(dir)) {
        n = host_join(out, dir, dlen, leaf, llen);
        if (n < 0) return -1;
        if (n + 14 > PY_PATHMAX) return -1;
        strcpy(out + n, ispkg ? "/__init__.py" : ".py");
        return (long)strlen(out);
    }
#ifdef PY_HOST_CMS
    {
        static const char lo[] = "abcdefghijklmnopqrstuvwxyz";
        static const char up[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        long i;
        const char *q;
        if (llen > 8 || dlen > 8 || llen < 1) return -1;
        for (i = 0; i < llen; i++) {
            q = leaf[i] ? strchr(lo, leaf[i]) : NULL;
            out[i] = q ? up[q - lo] : leaf[i];
        }
        out[llen] = 0;
        strcat(out, ispkg ? " PYI " : " PY ");
        strncat(out, dlen > 0 ? dir : "A", dlen > 0 ? (size_t)dlen : 1);
        return (long)strlen(out);
    }
#else
    n = host_join(out, dir, dlen, leaf, llen);
    if (n < 0) return -1;
    if (n + 14 > PY_PATHMAX) return -1;
    strcpy(out + n, ispkg ? PY_DIRSEP "__init__.py" : ".py");
    return (long)strlen(out);
#endif
}

/* ---- files -------------------------------------------------------------- */
static const PyEmbFile *host_embed_find(const char *path)
{
#ifdef PY_EMBED_LIBS
    size_t n = strlen(PY_EMBED_DIR);
    const PyEmbFile *e;
    if (!host_is_embed_path(path) || path[n] != '/') return NULL;
    for (e = pyemb_files; e->name; e++)
        if (strcmp(e->name, path + n + 1) == 0) return e;
#else
    (void)path;
#endif
    return NULL;
}

static FILE *host_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

/* whole file (or embedded module) into a malloc'ed, NUL terminated buffer */
static char *host_read_file(const char *path, long *len)
{
    FILE *fp;
    char *buf;
    long n = 0, cap = 4096;
    int c;
    const PyEmbFile *e = host_embed_find(path);
    if (e) {
        long off = 0, k = 0;
        buf = (char *)malloc((size_t)e->len + 1);
        if (!buf) return NULL;
        while (off < e->len) {
            long m = e->len - off < PYEMB_CHUNK ? e->len - off : PYEMB_CHUNK;
            const unsigned char *src = e->chunks[k++];
            long i;
            for (i = 0; i < m; i++) buf[off + i] = (char)host_from_ascii(src[i]);
            off += m;
        }
        buf[e->len] = 0;
        *len = e->len;
        return buf;
    }
    if (host_is_embed_path(path)) return NULL;
    fp = fopen(path, PY_RMODE);
    if (!fp) return NULL;
    buf = (char *)malloc((size_t)cap);
    if (!buf) { fclose(fp); return NULL; }
    while ((c = getc(fp)) != EOF) {
        if (n + 2 >= cap) {
            char *nb;
            cap *= 2;
            nb = (char *)realloc(buf, (size_t)cap);
            if (!nb) { free(buf); fclose(fp); return NULL; }
            buf = nb;
        }
        buf[n++] = (char)c;
    }
    if (ferror(fp)) { fclose(fp); free(buf); return NULL; }
    buf[n] = 0;
    fclose(fp);
    *len = n;
    return buf;
}

static int host_file_exists(const char *path)
{
    FILE *fp;
    if (host_embed_find(path)) return 1;
    fp = fopen(path, PY_RMODE);
    if (!fp) return 0;
    fclose(fp);
    return 1;
}
/* a directory opens with fopen on POSIX but cannot be read */
static int host_file_isdir(const char *path)
{
    FILE *fp = fopen(path, PY_RMODE);
    int isdir = 0;
    if (!fp) return 0;
    if (getc(fp) == EOF && ferror(fp)) isdir = 1;
    fclose(fp);
    return isdir;
}
static int host_file_isfile(const char *path)
{
    FILE *fp = fopen(path, PY_RMODE);
    int ok;
    if (!fp) return 0;
    ok = !(getc(fp) == EOF && ferror(fp));
    fclose(fp);
    return ok;
}
/* size in bytes, or -1 if the file cannot be opened */
static long host_file_size(const char *path)
{
    FILE *fp = fopen(path, PY_RMODE);
    long n;
    if (!fp) return -1L;
    fseek(fp, 0L, SEEK_END);
    n = ftell(fp);
    fclose(fp);
    return n;
}
static int host_file_remove(const char *path) { return remove(path); }
static int host_file_rename(const char *from, const char *to) { return rename(from, to); }

/* ---- environment -------------------------------------------------------- */
static const char *host_getenv(const char *name) { return getenv(name); }

/* the working directory into buf; 0 if it is not available */
static int host_getcwd(char *buf, long cap)
{
#ifdef PY_POSIX
    return getcwd(buf, (size_t)cap) != NULL;
#else
    (void)buf; (void)cap;
    return 0;
#endif
}

#ifdef PY_POSIX
extern char **environ;
#endif
/* NULL terminated "NAME=value" strings, or NULL when there is no table */
static char **host_environ(void)
{
#ifdef PY_POSIX
    return environ;
#else
    return NULL;
#endif
}

static int host_isatty_stdin(void)
{
#ifdef PY_POSIX
    return isatty(0);
#else
    return 0;
#endif
}

/* ---- time --------------------------------------------------------------- */
static double host_time(void)
{
#ifdef PY_POSIX
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
#else
    return (double)time(NULL);
#endif
}
static double host_clock(void)
{
    return (double)clock() / (double)CLOCKS_PER_SEC;
}
static void host_sleep(double d)
{
#ifdef PY_POSIX
    struct timespec ts;
    ts.tv_sec = (time_t)d;
    ts.tv_nsec = (long)((d - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
#else
    clock_t end = clock() + (clock_t)(d * (double)CLOCKS_PER_SEC);
    while (clock() < end) { }
#endif
}
/* an unpredictable number for seeding the random generators */
static unsigned long host_seed(void)
{
    return (unsigned long)time(NULL) * 1000003UL + (unsigned long)clock();
}

/* ---- sockets (IPv4 TCP) -------------------------------------------------
   The whole network interface of the interpreter.  Descriptors are small
   ints, addresses are 4 bytes in network order plus a port.  Calls return
   -1 on failure and leave the host error number in host_sock_err, which
   host_sock_strerror / host_sock_errkind describe.  A port to another TCP/IP
   stack (CMS TCPIP, MVS) reimplements this block; the PY_POSIX branch is the
   reference. */
static int host_sock_err;

/* error kinds for the exception class: 0 OSError, 1 refused, 2 reset, 3 aborted,
   4 broken pipe, 5 would block, 6 interrupted, 7 timed out, 8 in progress */
#ifdef PY_POSIX
static int host_sock_gai;           /* nonzero: host_sock_err came from name lookup */
static int host_sock_ok(void) { return 1; }
static int host_sock_errkind(int e)
{
    if (e == ECONNREFUSED) return 1;
    if (e == ECONNRESET) return 2;
    if (e == ECONNABORTED) return 3;
    if (e == EPIPE) return 4;
    if (e == EAGAIN || e == EWOULDBLOCK) return 5;
    if (e == EINTR) return 6;
    if (e == ETIMEDOUT) return 7;
    if (e == EINPROGRESS || e == EALREADY) return 8;
    return 0;
}
static const char *host_sock_strerror(int e)
{
    return host_sock_gai ? gai_strerror(e) : strerror(e);
}
static int host_sock_fail(void) { host_sock_err = errno; host_sock_gai = 0; return -1; }
static void host_sock_fill(struct sockaddr_in *sa, const unsigned char *ip, int port)
{
    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    memcpy(&sa->sin_addr, ip, 4);
    sa->sin_port = htons((unsigned short)port);
}
static void host_sock_unfill(const struct sockaddr_in *sa, unsigned char *ip, int *port)
{
    memcpy(ip, &sa->sin_addr, 4);
    *port = (int)ntohs(sa->sin_port);
}
static int host_sock_open(void)
{
    int fd;
    static int ign;
    if (!ign) { signal(SIGPIPE, SIG_IGN); ign = 1; }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    return fd < 0 ? host_sock_fail() : fd;
}
static int host_sock_close(int fd)
{
    return close(fd) < 0 ? host_sock_fail() : 0;
}
static int host_sock_bind(int fd, const unsigned char *ip, int port)
{
    struct sockaddr_in sa;
    host_sock_fill(&sa, ip, port);
    return bind(fd, (struct sockaddr *)&sa, (socklen_t)sizeof(sa)) < 0 ? host_sock_fail() : 0;
}
static int host_sock_listen(int fd, int backlog)
{
    return listen(fd, backlog) < 0 ? host_sock_fail() : 0;
}
static int host_sock_accept(int fd, unsigned char *ip, int *port)
{
    struct sockaddr_in sa;
    socklen_t n = (socklen_t)sizeof(sa);
    int r = accept(fd, (struct sockaddr *)&sa, &n);
    if (r < 0) return host_sock_fail();
    host_sock_unfill(&sa, ip, port);
    return r;
}
static int host_sock_connect(int fd, const unsigned char *ip, int port)
{
    struct sockaddr_in sa;
    host_sock_fill(&sa, ip, port);
    return connect(fd, (struct sockaddr *)&sa, (socklen_t)sizeof(sa)) < 0 ? host_sock_fail() : 0;
}
static long host_sock_send(int fd, const char *b, long n)
{
    long r;
#ifdef MSG_NOSIGNAL
    r = (long)send(fd, b, (size_t)n, MSG_NOSIGNAL);
#else
    r = (long)send(fd, b, (size_t)n, 0);
#endif
    return r < 0 ? (long)host_sock_fail() : r;
}
static long host_sock_recv(int fd, char *b, long n)
{
    long r = (long)recv(fd, b, (size_t)n, 0);
    return r < 0 ? (long)host_sock_fail() : r;
}
static int host_sock_shutdown(int fd, int how)
{
    return shutdown(fd, how) < 0 ? host_sock_fail() : 0;
}
static int host_sock_blocking(int fd, int on)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return host_sock_fail();
    fl = on ? (fl & ~O_NONBLOCK) : (fl | O_NONBLOCK);
    return fcntl(fd, F_SETFL, fl) < 0 ? host_sock_fail() : 0;
}
static int host_sock_getname(int fd, int peer, unsigned char *ip, int *port)
{
    struct sockaddr_in sa;
    socklen_t n = (socklen_t)sizeof(sa);
    int r = peer ? getpeername(fd, (struct sockaddr *)&sa, &n)
                 : getsockname(fd, (struct sockaddr *)&sa, &n);
    if (r < 0) return host_sock_fail();
    host_sock_unfill(&sa, ip, port);
    return 0;
}
static int host_sock_setopt(int fd, int level, int opt, long val)
{
    int v = (int)val;
    return setsockopt(fd, level, opt, &v, (socklen_t)sizeof(v)) < 0 ? host_sock_fail() : 0;
}
static int host_sock_getopt(int fd, int level, int opt, long *val)
{
    int v = 0;
    socklen_t n = (socklen_t)sizeof(v);
    if (getsockopt(fd, level, opt, &v, &n) < 0) return host_sock_fail();
    *val = (long)v;
    return 0;
}
/* the pending error of a connecting socket (0 when none) */
static int host_sock_soerror(int fd)
{
    long v = 0;
    if (host_sock_getopt(fd, SOL_SOCKET, SO_ERROR, &v) < 0) return host_sock_err;
    return (int)v;
}
/* wait for readability (mode 1) or writability (mode 2) of the descriptors; timeout < 0 waits
   forever.  ready[i] gets the mode bits that are ready.  Returns the count, or -1 */
static int host_sock_select(int n, const int *fds, const int *modes, int *ready, double timeout)
{
    fd_set rs, ws;
    struct timeval tv;
    int i, mx = -1, r;
    for (;;) {
        FD_ZERO(&rs); FD_ZERO(&ws);
        for (i = 0; i < n; i++) {
            if (fds[i] < 0 || fds[i] >= FD_SETSIZE) { host_sock_err = EBADF; return -1; }
            if (modes[i] & 1) FD_SET(fds[i], &rs);
            if (modes[i] & 2) FD_SET(fds[i], &ws);
            if (fds[i] > mx) mx = fds[i];
        }
        if (timeout >= 0) {
            tv.tv_sec = (time_t)timeout;
            tv.tv_usec = (long)((timeout - (double)tv.tv_sec) * 1000000.0);
        }
        r = select(mx + 1, &rs, &ws, NULL, timeout >= 0 ? &tv : NULL);
        if (r < 0 && errno == EINTR) continue;
        break;
    }
    if (r < 0) return host_sock_fail();
    for (i = 0; i < n; i++) {
        ready[i] = 0;
        if ((modes[i] & 1) && FD_ISSET(fds[i], &rs)) ready[i] |= 1;
        if ((modes[i] & 2) && FD_ISSET(fds[i], &ws)) ready[i] |= 2;
    }
    return r;
}
static int host_sock_hostname(char *buf, long cap)
{
    if (gethostname(buf, (size_t)cap) < 0) return host_sock_fail();
    buf[cap - 1] = 0;
    return 0;
}
/* host name or dotted quad to 4 address bytes */
static int host_sock_resolve(const char *name, unsigned char *ip)
{
    struct addrinfo hints, *res = NULL;
    int r;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    r = getaddrinfo(name, NULL, &hints, &res);
    if (r != 0 || !res) { host_sock_err = r; host_sock_gai = 1; return -1; }
    memcpy(ip, &((struct sockaddr_in *)res->ai_addr)->sin_addr, 4);
    freeaddrinfo(res);
    return 0;
}
/* named constants the socket module exports */
static int host_sock_const(const char *name, long *val)
{
    static const struct { const char *n; long v; } tab[] = {
        { "AF_UNSPEC", AF_UNSPEC }, { "AF_INET", AF_INET }, { "AF_INET6", AF_INET6 },
        { "SOCK_STREAM", SOCK_STREAM }, { "SOCK_DGRAM", SOCK_DGRAM },
        { "SOL_SOCKET", SOL_SOCKET }, { "SO_REUSEADDR", SO_REUSEADDR },
        { "SO_KEEPALIVE", SO_KEEPALIVE }, { "SO_RCVBUF", SO_RCVBUF }, { "SO_SNDBUF", SO_SNDBUF },
        { "SO_ERROR", SO_ERROR }, { "SO_BROADCAST", SO_BROADCAST },
        { "IPPROTO_TCP", IPPROTO_TCP }, { "TCP_NODELAY", TCP_NODELAY },
        { "SHUT_RD", SHUT_RD }, { "SHUT_WR", SHUT_WR }, { "SHUT_RDWR", SHUT_RDWR },
        { "SOMAXCONN", SOMAXCONN }, { "AI_PASSIVE", AI_PASSIVE }, { NULL, 0 }
    };
    int i;
    for (i = 0; tab[i].n; i++)
        if (!strcmp(tab[i].n, name)) { *val = tab[i].v; return 1; }
    return 0;
}
#else
/* no network: every call fails */
static int host_sock_ok(void) { return 0; }
static int host_sock_errkind(int e) { (void)e; return 0; }
static const char *host_sock_strerror(int e) { (void)e; return "sockets not available"; }
static int host_sock_open(void) { return -1; }
static int host_sock_close(int fd) { (void)fd; return -1; }
static int host_sock_bind(int fd, const unsigned char *ip, int port)
{ (void)fd; (void)ip; (void)port; return -1; }
static int host_sock_listen(int fd, int backlog) { (void)fd; (void)backlog; return -1; }
static int host_sock_accept(int fd, unsigned char *ip, int *port)
{ (void)fd; (void)ip; (void)port; return -1; }
static int host_sock_connect(int fd, const unsigned char *ip, int port)
{ (void)fd; (void)ip; (void)port; return -1; }
static long host_sock_send(int fd, const char *b, long n)
{ (void)fd; (void)b; (void)n; return -1; }
static long host_sock_recv(int fd, char *b, long n)
{ (void)fd; (void)b; (void)n; return -1; }
static int host_sock_shutdown(int fd, int how) { (void)fd; (void)how; return -1; }
static int host_sock_blocking(int fd, int on) { (void)fd; (void)on; return -1; }
static int host_sock_getname(int fd, int peer, unsigned char *ip, int *port)
{ (void)fd; (void)peer; (void)ip; (void)port; return -1; }
static int host_sock_setopt(int fd, int level, int opt, long val)
{ (void)fd; (void)level; (void)opt; (void)val; return -1; }
static int host_sock_getopt(int fd, int level, int opt, long *val)
{ (void)fd; (void)level; (void)opt; (void)val; return -1; }
static int host_sock_soerror(int fd) { (void)fd; return 0; }
static int host_sock_select(int n, const int *fds, const int *modes, int *ready, double timeout)
{ (void)n; (void)fds; (void)modes; (void)ready; (void)timeout; return -1; }
static int host_sock_hostname(char *buf, long cap) { (void)buf; (void)cap; return -1; }
static int host_sock_resolve(const char *name, unsigned char *ip)
{ (void)name; (void)ip; return -1; }
static int host_sock_const(const char *name, long *val)
{
    static const struct { const char *n; long v; } tab[] = {
        { "AF_UNSPEC", 0 }, { "AF_INET", 2 }, { "AF_INET6", 10 },
        { "SOCK_STREAM", 1 }, { "SOCK_DGRAM", 2 }, { "SOL_SOCKET", 1 }, { "SO_REUSEADDR", 2 },
        { "SO_KEEPALIVE", 9 }, { "SO_RCVBUF", 8 }, { "SO_SNDBUF", 7 }, { "SO_ERROR", 4 },
        { "SO_BROADCAST", 6 }, { "IPPROTO_TCP", 6 }, { "TCP_NODELAY", 1 },
        { "SHUT_RD", 0 }, { "SHUT_WR", 1 }, { "SHUT_RDWR", 2 }, { "SOMAXCONN", 128 },
        { "AI_PASSIVE", 1 }, { NULL, 0 }
    };
    int i;
    for (i = 0; tab[i].n; i++)
        if (!strcmp(tab[i].n, name)) { *val = tab[i].v; return 1; }
    return 0;
}
#endif
