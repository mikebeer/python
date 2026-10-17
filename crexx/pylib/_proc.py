"""_proc for the CREXX interpreter: child processes on top of the primitive _sys(cmd) (ADDRESS SYSTEM, sh -c).

Same interface as the native module of the C interpreter, so pylib/subprocess.py works unchanged:
  run(argv, input, timeout, cwd, inherit=False) -> (status, rc, stdout, stderr)   status 0 ok, 1 timeout, 2 cannot start
  spawn(argv, cwd, merge) -> (pid, stdin_fd, stdout_fd, stderr_fd), read, write, close, kill, wait
The child's streams go through files in a private temporary directory; a live child reads its
standard input from a feeder loop that picks up numbered chunk files, so write() reaches it at once.
Needs a POSIX sh, `timeout` (coreutils) and `setsid`.  Output is binary-exact (files, not captured text).
"""
import os
import time

_n = [0]
_procs = {}
_fds = {}
_nextfd = [1000]


def _q(s):
    return "'" + str(s).replace("'", "'\\''") + "'"


def _tmpdir():
    _n[0] += 1
    base = os.environ.get("TMPDIR", "/tmp") if hasattr(os, "environ") else "/tmp"
    t = int(time.time() * 1000) % 100000000
    d = "%s/_cxp_%d_%d" % (base, t, _n[0])
    os.mkdir(d)
    return d


def _rm(d):
    try:
        for f in os.listdir(d):
            try:
                os.remove(d + "/" + f)
            except OSError:
                pass
        os.rmdir(d)
    except OSError:
        pass


def _slurp(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return b""


def _cmdline(argv):
    return " ".join([_q(a) for a in argv])


def _exists_check(argv, d):
    a = argv[0]
    if "/" in a:
        return "[ -x %s ] || { : > %s/nf; exit 0; }" % (_q(a), d)
    return "command -v %s >/dev/null 2>&1 || { : > %s/nf; exit 0; }" % (_q(a), d)


def run(argv, data, timeout, cwd, inherit=False):
    d = _tmpdir()
    try:
        parts = []
        if cwd:
            parts.append("cd %s || { : > %s/nf; exit 0; }" % (_q(cwd), d))
        parts.append(_exists_check(argv, d))
        cmd = _cmdline(argv)
        if timeout is not None:
            cmd = "timeout -k 1 %s %s" % (repr(float(timeout)), cmd)
        if not inherit:
            if data is not None:
                with open(d + "/in", "wb") as f:
                    f.write(bytes(data))
                cmd += " < %s/in" % d
            else:
                cmd += " < /dev/null"
            cmd += " > %s/out 2> %s/err" % (d, d)
        parts.append(cmd)
        parts.append("echo $? > %s/rc" % d)
        _sys("; ".join(parts))
        if os.path.exists(d + "/nf"):
            return (2, 2, b"", b"")
        rcs = _slurp(d + "/rc").decode().strip()
        rc = int(rcs) if rcs else 1
        out = _slurp(d + "/out")
        err = _slurp(d + "/err")
        if timeout is not None and (rc == 124 or rc == 137):
            return (1, rc, out, err)
        if rc > 128 and rc < 160:
            rc = -(rc - 128)
        return (0, rc, out, err)
    finally:
        _rm(d)


# ---- live children ------------------------------------------------------------

_INNER = """echo $$ > %(d)s/pid
cd %(cwd)s || exit 127
( i=0; while :; do
    if [ -f %(d)s/in.$i ]; then cat %(d)s/in.$i; rm -f %(d)s/in.$i; i=$((i+1))
    elif [ -f %(d)s/in.close ]; then break
    elif [ ! -d /proc/%(me)s ]; then break
    else sleep 0.03; fi
  done ) | %(cmd)s > %(d)s/out %(errto)s
"""


def spawn(argv, cwd, merge):
    d = _tmpdir()
    chk = _exists_check(argv, d)
    if cwd:
        chk = "cd %s || { : > %s/nf; exit 0; }; %s" % (_q(cwd), d, chk)
    _sys(chk)
    if os.path.exists(d + "/nf"):
        _rm(d)
        raise OSError(2, "No such file or directory")
    errto = "2>&1" if merge == 1 else "2> " + d + "/err"
    script = _INNER % {"d": d, "cwd": _q(cwd or "."), "cmd": _cmdline(argv), "errto": errto, "me": os.getpid()}
    with open(d + "/inner.sh", "w") as f:
        f.write(script)
    # ADDRESS SYSTEM ends the process group of the command when it returns: the supervisor runs in a
    # session of its own (setsid), the child in a job-control group below it (set -m), so kill(-pid) spares the supervisor
    _sys("setsid sh -c 'exec 3>&- 4>&- 5>&- 6>&- 7>&- 8>&- 9>&-; set -m; sh %s/inner.sh & wait $!; echo $? > %s/rc' < /dev/null > /dev/null 2>&1 &" % (d, d))
    t0 = time.time()
    while not os.path.exists(d + "/pid") and time.time() - t0 < 5:
        time.sleep(0.01)
    pidtxt = _slurp(d + "/pid").decode().strip()
    pid = int(pidtxt) if pidtxt else 0
    _procs[pid] = {"d": d, "open": 0, "rc": None, "sig": 0, "chunk": 0}
    p = _procs[pid]
    fi = _newfd(pid, "in", 0)
    fo = _newfd(pid, "out", 0)
    fe = -1
    if merge == 0:
        fe = _newfd(pid, "err", 0)
    return (pid, fi, fo, fe)


def _newfd(pid, kind, off):
    _nextfd[0] += 1
    fd = _nextfd[0]
    _fds[fd] = [pid, kind, off]
    _procs[pid]["open"] += 1
    return fd


def _finished(pid):
    p = _procs[pid]
    if p["rc"] is not None:
        return True
    s = _slurp(p["d"] + "/rc").decode().strip()
    if s:
        rc = int(s)
        if p["sig"] and rc > 128:
            rc = -(rc - 128)
        elif rc > 128 and rc < 160 and p["sig"] == 0:
            rc = -(rc - 128)
        p["rc"] = rc
        return True
    return False


def read(fd, n, timeout):
    pid, kind, off = _fds[fd]
    p = _procs[pid]
    t0 = time.time()
    while True:
        done = _finished(pid)
        data = _slurp(p["d"] + "/" + kind)
        if len(data) > off:
            chunk = data[off:off + n]
            _fds[fd][2] = off + len(chunk)
            return chunk
        if done:
            return b""
        if timeout is not None and time.time() - t0 >= timeout:
            return None
        time.sleep(0.02)


def write(fd, data):
    pid, kind, off = _fds[fd]
    p = _procs[pid]
    d = p["d"]
    k = p["chunk"]
    p["chunk"] = k + 1
    with open(d + "/w.tmp", "wb") as f:
        f.write(bytes(data))
    os.rename(d + "/w.tmp", "%s/in.%d" % (d, k))
    return len(data)


def close(fd):
    if fd not in _fds:
        return
    pid, kind, off = _fds[fd]
    del _fds[fd]
    p = _procs[pid]
    if kind == "in":
        with open(p["d"] + "/in.close", "w") as f:
            f.write("")
    p["open"] -= 1


def kill(pid, sig):
    p = _procs.get(pid)
    if p is None or _finished(pid):
        return
    p["sig"] = sig
    _sys("kill -%d -- -%d 2>/dev/null || kill -%d %d 2>/dev/null" % (sig, pid, sig, pid))


def wait(pid, nohang):
    p = _procs[pid]
    while not _finished(pid):
        if nohang:
            return (False, None)
        time.sleep(0.02)
    return (True, p["rc"])
