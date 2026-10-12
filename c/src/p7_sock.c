/* =========================================================================
   p7_sock.c   the socket and select modules (IPv4 TCP)

   Pure interpreter side: every network call goes through the host_sock_*
   adapter in p0_os.c, which is the only place that knows BSD sockets.
   Without -DPY_POSIX the adapter fails every call and this module raises
   OSError('sockets not available') when a socket is created.

   A socket object is an instance of socket.socket with the attributes _fd
   (descriptor, -1 when closed) and _to (None, or the timeout in seconds;
   0.0 is non-blocking).  A descriptor is blocking exactly when _to is None;
   with a timeout it is non-blocking and every wait is a select on it.
   Descriptors of sockets that are not closed explicitly stay open until the
   process ends (there is no finalizer).
   ========================================================================= */
static Class *cls_socket, *cls_gaierror, *cls_herror;
static Str *S_sfd, *S_sto;
static double sock_deftimeout = -1.0;       /* socket.setdefaulttimeout; < 0 is None */

static void sock_need_host(void)
{
    if (!host_sock_ok()) throw_new(cls_OSError, "sockets not available");
}
/* OSError (or the subclass for the error kind) carrying errno and strerror */
static void raise_os_cls(Class *c, int err, const char *msg)
{
    Value a[2], ev;
    Inst *in;
    a[0] = mkint(err); a[1] = mkcstr(msg);
    ev = new_exc_args(c, a, 2);
    in = AS_INST(ev);
    amap_set(&in->a, intern_c("errno"), a[0]);
    amap_set(&in->a, intern_c("strerror"), a[1]);
    amap_set(&in->a, intern_c("filename"), NONE_V);
    throw_val(ev);
}
static void raise_os(int err)
{
    Class *c;
    switch (host_sock_errkind(err)) {
    case 1: c = cls_ConnectionRefusedError; break;
    case 2: c = cls_ConnectionResetError; break;
    case 3: c = cls_ConnectionAbortedError; break;
    case 4: c = cls_BrokenPipeError; break;
    case 5: case 8: c = cls_BlockingIOError; break;
    case 6: c = cls_InterruptedError; break;
    case 7: c = cls_TimeoutError; break;
    default: c = cls_OSError; break;
    }
    raise_os_cls(c, err, host_sock_strerror(err));
}
static void raise_lookup(void)
{
    raise_os_cls(cls_gaierror, host_sock_err, host_sock_strerror(host_sock_err));
}

/* ---- socket object accessors ------------------------------------------ */
static Inst *sock_self(Args *x)
{
    if (x->n < 1 || A(0).t != T_INST || !class_isa(AS_INST(A(0))->cls, cls_socket))
        throw_new(cls_TypeError, "descriptor requires a 'socket.socket' object");
    return AS_INST(A(0));
}
static int sock_getfd(Inst *in)
{
    Value *p = amap_get(&in->a, S_sfd);
    if (!p || p->t != T_INT || p->u.i < 0)
        raise_os_cls(cls_OSError, 9, "Bad file descriptor");
    return (int)p->u.i;
}
static double sock_getto(Inst *in)
{
    Value *p = amap_get(&in->a, S_sto);
    if (!p || p->t == T_NONE) return -1.0;
    return to_dbl(*p);
}
static void sock_setto(Inst *in, double to)
{
    amap_set(&in->a, S_sto, to < 0 ? NONE_V : mkfloat(to));
}
/* the descriptor mode follows the timeout: blocking only without one */
static void sock_apply_to(Inst *in, int fd, double to)
{
    sock_setto(in, to);
    if (host_sock_blocking(fd, to < 0) < 0) raise_os(host_sock_err);
}
static void sock_attach(Inst *in, int fd)
{
    amap_set(&in->a, S_sfd, mkint(fd));
    sock_apply_to(in, fd, sock_deftimeout);
}
static void sock_wait(int fd, int mode, double to)
{
    int ready = 0, r;
    r = host_sock_select(1, &fd, &mode, &ready, to);
    if (r < 0) raise_os(host_sock_err);
    if (r == 0) throw_new(cls_TimeoutError, "timed out");
}

/* ---- addresses -------------------------------------------------------- */
static int parse_ip4(const char *s, unsigned char *ip)
{
    int part = 0, val = -1;
    for (;; s++) {
        if (*s >= '0' && *s <= '9') {
            val = (val < 0 ? 0 : val * 10) + (*s - '0');
            if (val > 255) return 0;
        } else if (*s == '.' || *s == 0) {
            if (val < 0 || part > 3) return 0;
            ip[part++] = (unsigned char)val;
            val = -1;
            if (*s == 0) break;
        } else return 0;
    }
    return part == 4;
}
static Value ip_value(const unsigned char *ip)
{
    char t[24];
    sprintf(t, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return mkcstr(t);
}
static Value addr_value(const unsigned char *ip, int port)
{
    Value v[2];
    v[0] = ip_value(ip); v[1] = mkint(port);
    return mktuple_from(v, 2);
}
static void resolve_host(const char *h, unsigned char *ip, int passive)
{
    if (h[0] == 0) {
        memset(ip, 0, 4);
        if (!passive) { ip[0] = 0; }
        return;
    }
    if (!strcmp(h, "<broadcast>")) { memset(ip, 255, 4); return; }
    if (parse_ip4(h, ip)) return;
    if (host_sock_resolve(h, ip) < 0) raise_lookup();
}
static void parse_addr(Value a, unsigned char *ip, int *port)
{
    List *l;
    long p;
    if (a.t != T_TUPLE || AS_LIST(a)->n != 2)
        throw_new(cls_TypeError, "AF_INET address must be tuple, not %.40s", type_name(a));
    l = AS_LIST(a);
    if (l->a[0].t != T_STR)
        throw_new(cls_TypeError, "str, bytes or bytearray expected, not %.40s", type_name(l->a[0]));
    if (l->a[1].t != T_INT && l->a[1].t != T_BOOL)
        throw_new(cls_TypeError, "'%.40s' object cannot be interpreted as an integer",
            type_name(l->a[1]));
    p = l->a[1].u.i;
    if (p < 0 || p > 65535) throw_new(cls_OverflowError, "bind(): port must be 0-65535.");
    resolve_host(AS_STR(l->a[0])->s, ip, 1);
    *port = (int)p;
}

/* ---- the socket class ------------------------------------------------- */
BI(sk_init)
{
    Inst *in = sock_self(x);
    long fam = 2, typ = 1;
    Value v;
    need(x, 1, 5, "socket");
    if (x->n > 1) fam = need_long(A(1), "family");
    if (x->n > 2) typ = need_long(A(2), "type");
    v = kwget(x, "family"); if (v.t != T_UNSET) fam = need_long(v, "family");
    v = kwget(x, "type"); if (v.t != T_UNSET) typ = need_long(v, "type");
    v = kwget(x, "fileno");
    if (x->n > 4) v = A(4);
    sock_need_host();
    if (fam != 2) raise_os_cls(cls_OSError, 97, "Address family not supported by protocol");
    if ((typ & 0xf) != 1) raise_os_cls(cls_OSError, 94, "Socket type not supported");
    if (v.t != T_UNSET && v.t != T_NONE) {
        long fd = need_long(v, "fileno");
        sock_attach(in, (int)fd);
    } else {
        int fd = host_sock_open();
        if (fd < 0) raise_os(host_sock_err);
        sock_attach(in, fd);
    }
    amap_set(&in->a, intern_c("family"), mkint(fam));
    amap_set(&in->a, intern_c("type"), mkint(1));
    amap_set(&in->a, intern_c("proto"), mkint(0));
    return NONE_V;
}
BI(sk_fileno)
{
    Value *p = amap_get(&sock_self(x)->a, S_sfd);
    return p ? *p : mkint(-1);
}
BI(sk_close)
{
    Inst *in = sock_self(x);
    Value *p = amap_get(&in->a, S_sfd);
    if (p && p->t == T_INT && p->u.i >= 0) {
        int fd = (int)p->u.i;
        amap_set(&in->a, S_sfd, mkint(-1));
        host_sock_close(fd);
    }
    return NONE_V;
}
BI(sk_detach)
{
    Inst *in = sock_self(x);
    Value *p = amap_get(&in->a, S_sfd);
    Value r = p ? *p : mkint(-1);
    amap_set(&in->a, S_sfd, mkint(-1));
    return r;
}
BI(sk_enter) { (void)sock_self(x); return A(0); }
BI(sk_exit)
{
    sk_close(x);
    return FALSE_V;
}
BI(sk_settimeout)
{
    Inst *in = sock_self(x);
    double to = -1.0;
    need(x, 2, 2, "settimeout");
    if (A(1).t != T_NONE) {
        if (!is_num(A(1))) throw_new(cls_TypeError, "Timeout value must be int or float");
        to = to_dbl(A(1));
        if (to < 0) throw_new(cls_ValueError, "Timeout value out of range");
    }
    sock_apply_to(in, sock_getfd(in), to);
    return NONE_V;
}
BI(sk_gettimeout)
{
    Inst *in = sock_self(x);
    double to = sock_getto(in);
    return to < 0 ? NONE_V : mkfloat(to);
}
BI(sk_setblocking)
{
    Inst *in = sock_self(x);
    need(x, 2, 2, "setblocking");
    sock_apply_to(in, sock_getfd(in), py_truthy(A(1)) ? -1.0 : 0.0);
    return NONE_V;
}
BI(sk_getblocking)
{
    return mkbool(sock_getto(sock_self(x)) < 0);
}
BI(sk_bind)
{
    Inst *in = sock_self(x);
    unsigned char ip[4];
    int port;
    need(x, 2, 2, "bind");
    parse_addr(A(1), ip, &port);
    if (host_sock_bind(sock_getfd(in), ip, port) < 0) raise_os(host_sock_err);
    return NONE_V;
}
BI(sk_listen)
{
    Inst *in = sock_self(x);
    long n = 128;
    need(x, 1, 2, "listen");
    if (x->n > 1) n = need_long(A(1), "backlog");
    if (host_sock_listen(sock_getfd(in), (int)(n < 0 ? 0 : n)) < 0) raise_os(host_sock_err);
    return NONE_V;
}
/* connect; returns 0 or the error number (nothing is raised for a timeout) */
static int sock_connect_to(Inst *in, const unsigned char *ip, int port)
{
    int fd = sock_getfd(in), r, e;
    double to = sock_getto(in);
    r = host_sock_connect(fd, ip, port);
    if (r == 0) return 0;
    e = host_sock_err;
    if (host_sock_errkind(e) == 6 || (host_sock_errkind(e) == 8 && to != 0.0)) {
        /* the connection continues in the background */
        sock_wait(fd, 2, to);
        e = host_sock_soerror(fd);
        return e;
    }
    return e;
}
BI(sk_connect)
{
    Inst *in = sock_self(x);
    unsigned char ip[4];
    int port, e;
    need(x, 2, 2, "connect");
    parse_addr(A(1), ip, &port);
    e = sock_connect_to(in, ip, port);
    if (e) raise_os(e);
    return NONE_V;
}
BI(sk_connect_ex)
{
    Inst *in = sock_self(x);
    unsigned char ip[4];
    int port;
    need(x, 2, 2, "connect_ex");
    parse_addr(A(1), ip, &port);
    return mkint(sock_connect_to(in, ip, port));
}
BI(sk_accept)
{
    Inst *in = sock_self(x);
    int fd = sock_getfd(in), nf;
    double to = sock_getto(in);
    unsigned char ip[4];
    int port;
    Inst *ni;
    Value nv, tv[2];
    for (;;) {
        nf = host_sock_accept(fd, ip, &port);
        if (nf >= 0) break;
        if (host_sock_errkind(host_sock_err) == 6) continue;
        if (host_sock_errkind(host_sock_err) == 5 && to > 0) { sock_wait(fd, 1, to); continue; }
        raise_os(host_sock_err);
    }
    ni = new_inst(in->cls);
    nv = mkobj(ni, T_INST);
    VPUSH(nv);
    sock_attach(ni, nf);
    amap_set(&ni->a, intern_c("family"), mkint(2));
    amap_set(&ni->a, intern_c("type"), mkint(1));
    amap_set(&ni->a, intern_c("proto"), mkint(0));
    tv[0] = nv; tv[1] = addr_value(ip, port);
    nv = mktuple_from(tv, 2);
    vsp--;
    return nv;
}
/* one send honouring the timeout; returns the byte count */
static long sock_send_some(int fd, double to, const char *p, long n)
{
    for (;;) {
        long r = host_sock_send(fd, p, n);
        if (r >= 0) return r;
        if (host_sock_errkind(host_sock_err) == 6) continue;
        if (host_sock_errkind(host_sock_err) == 5 && to > 0) { sock_wait(fd, 2, to); continue; }
        raise_os(host_sock_err);
    }
}
BI(sk_send)
{
    Inst *in = sock_self(x);
    const char *p;
    long n;
    need(x, 2, 3, "send");
    if (!bytes_get(A(1), &p, &n))
        throw_new(cls_TypeError, "a bytes-like object is required, not '%.40s'", type_name(A(1)));
    return mkint(sock_send_some(sock_getfd(in), sock_getto(in), p, n));
}
BI(sk_sendall)
{
    Inst *in = sock_self(x);
    const char *p;
    long n, done = 0;
    int fd;
    double to;
    Value hold;
    need(x, 2, 3, "sendall");
    if (!bytes_get(A(1), &p, &n))
        throw_new(cls_TypeError, "a bytes-like object is required, not '%.40s'", type_name(A(1)));
    fd = sock_getfd(in);
    to = sock_getto(in);
    hold = A(1);
    VPUSH(hold);
    while (done < n) done += sock_send_some(fd, to, p + done, n - done);
    vsp--;
    return NONE_V;
}
/* receive up to n bytes into buf; returns the count */
static long sock_recv_some(int fd, double to, char *buf, long n)
{
    for (;;) {
        long r = host_sock_recv(fd, buf, n);
        if (r >= 0) return r;
        if (host_sock_errkind(host_sock_err) == 6) continue;
        if (host_sock_errkind(host_sock_err) == 5 && to > 0) { sock_wait(fd, 1, to); continue; }
        raise_os(host_sock_err);
    }
}
BI(sk_recv)
{
    Inst *in = sock_self(x);
    long n, r;
    char *buf;
    int fd;
    double to;
    Value res;
    need(x, 2, 3, "recv");
    n = need_long(A(1), "bufsize");
    if (n < 0) throw_new(cls_ValueError, "negative buffer size in recv");
    fd = sock_getfd(in);
    to = sock_getto(in);
    buf = (char *)xmalloc((size_t)n + 1);
    for (;;) {
        r = host_sock_recv(fd, buf, n);
        if (r >= 0) break;
        if (host_sock_errkind(host_sock_err) == 6) continue;
        if (host_sock_errkind(host_sock_err) == 5 && to > 0) {
            int mode = 1, ready = 0, w = host_sock_select(1, &fd, &mode, &ready, to);
            if (w < 0) { int e = host_sock_err; free(buf); raise_os(e); }
            if (w == 0) { free(buf); throw_new(cls_TimeoutError, "timed out"); }
            continue;
        }
        { int e = host_sock_err; free(buf); raise_os(e); }
    }
    res = mkbytes(buf, r);
    free(buf);
    return res;
}
BI(sk_recv_into)
{
    Inst *in = sock_self(x);
    long n = 0, r;
    if (x->n < 2 || A(1).t != T_BARR)
        throw_new(cls_TypeError,
            "recv_into() argument 'buffer' must be a writable bytes-like object");
    n = x->n > 2 ? need_long(A(2), "nbytes") : AS_BARR(A(1))->n;
    if (n == 0) n = AS_BARR(A(1))->n;
    if (n > AS_BARR(A(1))->n) throw_new(cls_ValueError,
        "nbytes is greater than the length of the buffer");
    r = sock_recv_some(sock_getfd(in), sock_getto(in), AS_BARR(A(1))->s, n);
    return mkint(r);
}
BI(sk_shutdown)
{
    Inst *in = sock_self(x);
    need(x, 2, 2, "shutdown");
    if (host_sock_shutdown(sock_getfd(in), (int)need_long(A(1), "how")) < 0)
        raise_os(host_sock_err);
    return NONE_V;
}
BI(sk_setsockopt)
{
    Inst *in = sock_self(x);
    long v = 0;
    need(x, 4, 4, "setsockopt");
    if (A(3).t == T_BOOL || A(3).t == T_INT) v = A(3).u.i;
    else throw_new(cls_TypeError, "a bytes-like object is required, not '%.40s'", type_name(A(3)));
    if (host_sock_setopt(sock_getfd(in), (int)need_long(A(1), "level"),
            (int)need_long(A(2), "optname"), v) < 0)
        raise_os(host_sock_err);
    return NONE_V;
}
BI(sk_getsockopt)
{
    Inst *in = sock_self(x);
    long v = 0;
    need(x, 3, 4, "getsockopt");
    if (host_sock_getopt(sock_getfd(in), (int)need_long(A(1), "level"),
            (int)need_long(A(2), "optname"), &v) < 0)
        raise_os(host_sock_err);
    return mkint(v);
}
static Value sock_name(Args *x, int peer)
{
    Inst *in = sock_self(x);
    unsigned char ip[4];
    int port;
    if (host_sock_getname(sock_getfd(in), peer, ip, &port) < 0) raise_os(host_sock_err);
    return addr_value(ip, port);
}
BI(sk_getsockname) { return sock_name(x, 0); }
BI(sk_getpeername) { return sock_name(x, 1); }
BI(sk_repr)
{
    Inst *in = sock_self(x);
    Value *p = amap_get(&in->a, S_sfd);
    char t[120];
    sprintf(t, "<socket.socket fd=%ld, family=2, type=1, proto=0>", p ? p->u.i : -1L);
    return mkcstr(t);
}

/* ---- module functions ------------------------------------------------- */
static int little_endian(void)
{
    unsigned int one = 1;
    return *(unsigned char *)&one == 1;
}
static unsigned long swap16(unsigned long v) { return ((v & 0xffUL) << 8) | ((v >> 8) & 0xffUL); }
static unsigned long swap32(unsigned long v)
{
    return ((v & 0xffUL) << 24) | ((v & 0xff00UL) << 8) | ((v >> 8) & 0xff00UL) |
           ((v >> 24) & 0xffUL);
}
BI(m_htons)
{
    long v;
    need(x, 1, 1, "htons");
    v = need_long(A(0), "x");
    if (v < 0 || v > 65535) throw_new(cls_OverflowError,
        "htons: Python int too large to convert to C 16-bit unsigned integer");
    return mkint((long)(little_endian() ? swap16((unsigned long)v) : (unsigned long)v));
}
BI(m_htonl)
{
    long v;
    need(x, 1, 1, "htonl");
    v = need_long(A(0), "x");
    if (v < 0 || v > 4294967295L)
        throw_new(cls_OverflowError, "int larger than 32 bits");
    return mkint((long)(little_endian() ? swap32((unsigned long)v) : (unsigned long)v));
}
BI(m_inet_aton)
{
    unsigned char ip[4];
    need(x, 1, 1, "inet_aton");
    if (A(0).t != T_STR) throw_new(cls_TypeError, "inet_aton() argument 1 must be str, not %.40s",
        type_name(A(0)));
    if (!parse_ip4(AS_STR(A(0))->s, ip))
        raise_os_cls(cls_OSError, 22, "illegal IP address string passed to inet_aton");
    return mkbytes((const char *)ip, 4);
}
BI(m_inet_pton)
{
    unsigned char ip[4];
    need(x, 2, 2, "inet_pton");
    if (need_long(A(0), "af") != 2) raise_os_cls(cls_OSError, 97,
        "Address family not supported by protocol");
    if (A(1).t != T_STR || !parse_ip4(AS_STR(A(1))->s, ip))
        raise_os_cls(cls_OSError, 22, "illegal IP address string passed to inet_pton");
    return mkbytes((const char *)ip, 4);
}
BI(m_inet_ntoa)
{
    const char *p;
    long n;
    need(x, 1, 1, "inet_ntoa");
    if (!bytes_get(A(0), &p, &n)) throw_new(cls_TypeError,
        "a bytes-like object is required, not '%.40s'", type_name(A(0)));
    if (n != 4) raise_os_cls(cls_OSError, 22, "packed IP wrong length for inet_ntoa");
    return ip_value((const unsigned char *)p);
}
BI(m_inet_ntop)
{
    const char *p;
    long n;
    need(x, 2, 2, "inet_ntop");
    if (!bytes_get(A(1), &p, &n)) throw_new(cls_TypeError,
        "a bytes-like object is required, not '%.40s'", type_name(A(1)));
    if (n != 4) throw_new(cls_ValueError, "invalid length of packed IP address string");
    return ip_value((const unsigned char *)p);
}
BI(m_gethostname)
{
    char buf[256];
    (void)x;
    sock_need_host();
    if (host_sock_hostname(buf, (long)sizeof(buf)) < 0) raise_os(host_sock_err);
    return mkcstr(buf);
}
BI(m_gethostbyname)
{
    unsigned char ip[4];
    need(x, 1, 1, "gethostbyname");
    if (A(0).t != T_STR) throw_new(cls_TypeError,
        "gethostbyname() argument 1 must be str, not %.40s", type_name(A(0)));
    sock_need_host();
    resolve_host(AS_STR(A(0))->s, ip, 0);
    return ip_value(ip);
}
BI(m_getfqdn) { return m_gethostname(x); }
static long service_port(const char *name)
{
    static const struct { const char *n; long p; } tab[] = {
        { "ftp", 21 }, { "ssh", 22 }, { "telnet", 23 }, { "smtp", 25 }, { "domain", 53 },
        { "http", 80 }, { "pop3", 110 }, { "ntp", 123 }, { "imap", 143 }, { "https", 443 },
        { NULL, 0 }
    };
    int i;
    for (i = 0; tab[i].n; i++) if (!strcmp(tab[i].n, name)) return tab[i].p;
    return -1;
}
BI(m_getservbyname)
{
    long p;
    need(x, 1, 2, "getservbyname");
    p = A(0).t == T_STR ? service_port(AS_STR(A(0))->s) : -1;
    if (p < 0) throw_new(cls_OSError, "service/proto not found");
    return mkint(p);
}
BI(m_getaddrinfo)
{
    unsigned char ip[4];
    long port = 0, typ = 0, flags = 0, k;
    Value hv = x->n > 0 ? A(0) : UNSET_V, pv = x->n > 1 ? A(1) : NONE_V, v;
    List *res;
    Value rv;
    need(x, 2, 6, "getaddrinfo");
    if (x->n > 3) typ = need_long(A(3), "type");
    if (x->n > 5) flags = need_long(A(5), "flags");
    v = kwget(x, "type"); if (v.t != T_UNSET) typ = need_long(v, "type");
    v = kwget(x, "flags"); if (v.t != T_UNSET) flags = need_long(v, "flags");
    sock_need_host();
    if (hv.t == T_NONE) {
        memset(ip, 0, 4);
        if (!(flags & 1)) { ip[0] = 127; ip[3] = 1; }
    } else if (hv.t == T_STR) resolve_host(AS_STR(hv)->s, ip, 1);
    else throw_new(cls_TypeError, "getaddrinfo() argument 1 must be str, bytes or None, not %.40s",
        type_name(hv));
    if (pv.t == T_INT) port = pv.u.i;
    else if (pv.t == T_STR) {
        char *end;
        port = strtol(AS_STR(pv)->s, &end, 10);
        if (*end || end == AS_STR(pv)->s) {
            port = service_port(AS_STR(pv)->s);
            if (port < 0) raise_os_cls(cls_gaierror, -8, "Servname not supported for ai_socktype");
        }
    } else if (pv.t != T_NONE)
        throw_new(cls_TypeError, "getaddrinfo() argument 2 must be int, str or None, not %.40s",
            type_name(pv));
    res = list_new(2);
    rv = mkobj(res, T_LIST);
    VPUSH(rv);
    for (k = 1; k <= 2; k++) {
        Value e[5], t;
        if (typ != 0 && typ != k) continue;
        e[0] = mkint(2); e[1] = mkint(k); e[2] = mkint(k == 1 ? 6 : 17);
        e[3] = mkstr("", 0); e[4] = addr_value(ip, (int)port);
        t = mktuple_from(e, 5);
        list_push(res, t);
    }
    vsp--;
    return rv;
}
BI(m_getdefaulttimeout)
{
    (void)x;
    return sock_deftimeout < 0 ? NONE_V : mkfloat(sock_deftimeout);
}
BI(m_setdefaulttimeout)
{
    need(x, 1, 1, "setdefaulttimeout");
    if (A(0).t == T_NONE) sock_deftimeout = -1.0;
    else {
        if (!is_num(A(0))) throw_new(cls_TypeError, "Timeout value must be int or float");
        sock_deftimeout = to_dbl(A(0));
        if (sock_deftimeout < 0) { sock_deftimeout = -1.0; throw_new(cls_ValueError,
            "Timeout value out of range"); }
    }
    return NONE_V;
}
BI(m_create_connection)
{
    Value addr, tmo, sv;
    unsigned char ip[4];
    int port, e, fd;
    Inst *in;
    need(x, 1, 3, "create_connection");
    addr = A(0);
    tmo = x->n > 1 ? A(1) : kwget(x, "timeout");
    parse_addr(addr, ip, &port);
    sock_need_host();
    fd = host_sock_open();
    if (fd < 0) raise_os(host_sock_err);
    in = new_inst(cls_socket);
    sv = mkobj(in, T_INST);
    VPUSH(sv);
    sock_attach(in, fd);
    amap_set(&in->a, intern_c("family"), mkint(2));
    amap_set(&in->a, intern_c("type"), mkint(1));
    amap_set(&in->a, intern_c("proto"), mkint(0));
    if (tmo.t != T_UNSET && tmo.t != T_NONE) {
        if (!is_num(tmo) || to_dbl(tmo) < 0) throw_new(cls_ValueError,
            "Timeout value out of range");
        sock_apply_to(in, fd, to_dbl(tmo));
    }
    e = sock_connect_to(in, ip, port);
    if (e) {
        host_sock_close(fd);
        amap_set(&in->a, S_sfd, mkint(-1));
        raise_os(e);
    }
    vsp--;
    return sv;
}

/* ---- select ----------------------------------------------------------- */
static int obj_fileno(Value o)
{
    if (o.t == T_INT || o.t == T_BOOL) return (int)o.u.i;
    if (o.t == T_INST) {
        Value *m = class_find(AS_INST(o)->cls, intern_c("fileno"));
        if (m) {
            Value r = call1(*m, o);
            if (r.t == T_INT) return (int)r.u.i;
        }
    }
    throw_new(cls_TypeError, "argument must be an int, or have a fileno() method.");
    return -1;
}
BI(sel_select)
{
    long nr, nw, i, total;
    List *rl, *wl;
    int *fds, *modes, *ready;
    double to = -1.0;
    Value tv, out[3], ov;
    List *ol[3];
    need(x, 3, 4, "select");
    tv = x->n > 3 ? A(3) : kwget(x, "timeout");
    if (tv.t != T_UNSET && tv.t != T_NONE) {
        if (!is_num(tv)) throw_new(cls_TypeError, "timeout must be a float or None");
        to = to_dbl(tv);
        if (to < 0) throw_new(cls_ValueError, "timeout must be non-negative");
    }
    if ((A(0).t != T_LIST && A(0).t != T_TUPLE) || (A(1).t != T_LIST && A(1).t != T_TUPLE) ||
        (A(2).t != T_LIST && A(2).t != T_TUPLE))
        throw_new(cls_TypeError, "select() arguments must be lists or tuples");
    rl = AS_LIST(A(0)); wl = AS_LIST(A(1));
    nr = rl->n; nw = wl->n;
    total = nr + nw;
    fds = (int *)xmalloc(sizeof(int) * (size_t)(total + 1) * 3);
    modes = fds + total + 1;
    ready = modes + total + 1;
    for (i = 0; i < nr; i++) { fds[i] = obj_fileno(rl->a[i]); modes[i] = 1; }
    for (i = 0; i < nw; i++) { fds[nr + i] = obj_fileno(wl->a[i]); modes[nr + i] = 2; }
    sock_need_host();
    if (total == 0) {
        if (to > 0) host_sleep(to);
    } else if (host_sock_select((int)total, fds, modes, ready, to) < 0) {
        int e = host_sock_err;
        free(fds);
        raise_os(e);
    } else {
        /* done below */
    }
    ol[0] = list_new(2); out[0] = mkobj(ol[0], T_LIST); VPUSH(out[0]);
    ol[1] = list_new(2); out[1] = mkobj(ol[1], T_LIST); VPUSH(out[1]);
    ol[2] = list_new(1); out[2] = mkobj(ol[2], T_LIST); VPUSH(out[2]);
    if (total > 0) {
        for (i = 0; i < nr; i++) if (ready[i] & 1) list_push(ol[0], rl->a[i]);
        for (i = 0; i < nw; i++) if (ready[nr + i] & 2) list_push(ol[1], wl->a[i]);
    }
    free(fds);
    ov = mktuple_from(out, 3);
    vsp -= 3;
    return ov;
}

/* ---- initialisation --------------------------------------------------- */
static void sock_init(void)
{
    Module *m, *sm;
    static const char *const cn[] = {
        "AF_UNSPEC", "AF_INET", "AF_INET6", "SOCK_STREAM", "SOCK_DGRAM", "SOL_SOCKET",
        "SO_REUSEADDR", "SO_KEEPALIVE", "SO_RCVBUF", "SO_SNDBUF", "SO_ERROR", "SO_BROADCAST",
        "IPPROTO_TCP", "TCP_NODELAY", "SHUT_RD", "SHUT_WR", "SHUT_RDWR", "SOMAXCONN",
        "AI_PASSIVE", NULL
    };
    int i;
    S_sfd = intern_c("_fd"); S_sto = intern_c("_to");
    m = new_module("socket");
    cls_socket = mod_class("socket", cls_object, "socket");
    cls_socket->flags |= CF_BUILTIN;
    def_bi(cls_socket, "__init__", sk_init); def_bi(cls_socket, "fileno", sk_fileno);
    def_bi(cls_socket, "close", sk_close); def_bi(cls_socket, "detach", sk_detach);
    def_bi(cls_socket, "__enter__", sk_enter); def_bi(cls_socket, "__exit__", sk_exit);
    def_bi(cls_socket, "settimeout", sk_settimeout); def_bi(cls_socket, "gettimeout",
        sk_gettimeout);
    def_bi(cls_socket, "setblocking", sk_setblocking);
    def_bi(cls_socket, "getblocking", sk_getblocking);
    def_bi(cls_socket, "bind", sk_bind); def_bi(cls_socket, "listen", sk_listen);
    def_bi(cls_socket, "connect", sk_connect); def_bi(cls_socket, "connect_ex", sk_connect_ex);
    def_bi(cls_socket, "accept", sk_accept); def_bi(cls_socket, "send", sk_send);
    def_bi(cls_socket, "sendall", sk_sendall); def_bi(cls_socket, "recv", sk_recv);
    def_bi(cls_socket, "recv_into", sk_recv_into); def_bi(cls_socket, "shutdown", sk_shutdown);
    def_bi(cls_socket, "setsockopt", sk_setsockopt); def_bi(cls_socket, "getsockopt",
        sk_getsockopt);
    def_bi(cls_socket, "getsockname", sk_getsockname);
    def_bi(cls_socket, "getpeername", sk_getpeername);
    def_bi(cls_socket, "__repr__", sk_repr);
    mod_val(m, "socket", mkobj(cls_socket, T_CLASS));
    mod_val(m, "SocketType", mkobj(cls_socket, T_CLASS));
    cls_gaierror = mod_class("gaierror", cls_OSError, "socket");
    cls_herror = mod_class("herror", cls_OSError, "socket");
    mod_val(m, "gaierror", mkobj(cls_gaierror, T_CLASS));
    mod_val(m, "herror", mkobj(cls_herror, T_CLASS));
    mod_val(m, "error", mkobj(cls_OSError, T_CLASS));
    mod_val(m, "timeout", mkobj(cls_TimeoutError, T_CLASS));
    for (i = 0; cn[i]; i++) {
        long v;
        if (host_sock_const(cn[i], &v)) mod_val(m, cn[i], mkint(v));
    }
    mod_def(m, "htons", m_htons); mod_def(m, "ntohs", m_htons);
    mod_def(m, "htonl", m_htonl); mod_def(m, "ntohl", m_htonl);
    mod_def(m, "inet_aton", m_inet_aton); mod_def(m, "inet_ntoa", m_inet_ntoa);
    mod_def(m, "inet_pton", m_inet_pton); mod_def(m, "inet_ntop", m_inet_ntop);
    mod_def(m, "gethostname", m_gethostname); mod_def(m, "gethostbyname", m_gethostbyname);
    mod_def(m, "getfqdn", m_getfqdn); mod_def(m, "getservbyname", m_getservbyname);
    mod_def(m, "getaddrinfo", m_getaddrinfo);
    mod_def(m, "getdefaulttimeout", m_getdefaulttimeout);
    mod_def(m, "setdefaulttimeout", m_setdefaulttimeout);
    mod_def(m, "create_connection", m_create_connection);
    sm = new_module("select");
    mod_def(sm, "select", sel_select);
}
