"""Minimal subprocess on top of the native _proc.run (POSIX builds only).

Without stdout/stderr/input/capture_output the child inherits the terminal (editors, pagers).
run / call / check_call / check_output / Popen(...).communicate().  A child is
always run to completion in one step; Popen defers the start until
communicate(), wait() or poll() -- unless stdin=PIPE and stdout=PIPE are both given: then the
child starts at once with live pipes (write to p.stdin, read with p.stdout.read1(n, timeout)
or p.read_nonblock(timeout), send_signal/kill/terminate, poll/wait).  The child leads its own
process group and kill()/terminate() signal the group.  Not supported: preexec_fn.
"""
import _proc

PIPE = -1
STDOUT = -2
DEVNULL = -3


class SubprocessError(Exception):
    pass


class CalledProcessError(SubprocessError):
    def __init__(self, returncode, cmd, output=None, stderr=None):
        self.returncode = returncode
        self.cmd = cmd
        self.output = output
        self.stdout = output
        self.stderr = stderr

    def __str__(self):
        return "Command '%s' returned non-zero exit status %d." % (self.cmd, self.returncode)


class TimeoutExpired(SubprocessError):
    def __init__(self, cmd, timeout, output=None, stderr=None):
        self.cmd = cmd
        self.timeout = timeout
        self.output = output
        self.stdout = output
        self.stderr = stderr

    def __str__(self):
        return "Command '%s' timed out after %s seconds" % (self.cmd, self.timeout)


class CompletedProcess:
    def __init__(self, args, returncode, stdout=None, stderr=None):
        self.args = args
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr

    def check_returncode(self):
        if self.returncode:
            raise CalledProcessError(self.returncode, self.args, self.stdout, self.stderr)

    def __repr__(self):
        return "CompletedProcess(args=%r, returncode=%r)" % (self.args, self.returncode)


def _argv(args, shell):
    if isinstance(args, str):
        if shell:
            return ["/bin/sh", "-c", args]
        return [args]
    args = [str(a) for a in args]
    if shell:
        return ["/bin/sh", "-c", " ".join(args)]
    return args


def _exec(args, input, timeout, cwd, shell, stderr, text, inherit=False):
    argv = _argv(args, shell)
    data = None
    if input is not None:
        data = input.encode("utf-8") if isinstance(input, str) else bytes(input)
    st, rc, out, err = _proc.run(argv, data, timeout, cwd, inherit)
    if st == 2:
        if rc == 2:
            raise FileNotFoundError(2, "No such file or directory: '%s'" % argv[0])
        raise OSError(rc, "cannot start '%s'" % argv[0])
    if stderr == STDOUT:
        out = out + err
        err = b""
    if text:
        out = out.decode("utf-8", "replace")
        err = err.decode("utf-8", "replace")
    if st == 1:
        raise TimeoutExpired(args, timeout, out, err)
    return rc, out, err


def run(args, input=None, capture_output=False, timeout=None, check=False, cwd=None,
        shell=False, stdout=None, stderr=None, text=False, universal_newlines=False,
        encoding=None, **kw):
    text = text or universal_newlines or encoding is not None
    inherit = stdout is None and stderr is None and not capture_output and input is None and timeout is None
    rc, out, err = _exec(args, input, timeout, cwd, shell, stderr, text, inherit)
    if not (capture_output or stdout == PIPE):
        out = None
    if not (capture_output or stderr == PIPE):
        err = None
    cp = CompletedProcess(args, rc, out, err)
    if check:
        cp.check_returncode()
    return cp


def call(args, **kw):
    return run(args, **kw).returncode


def check_call(args, **kw):
    run(args, check=True, **kw)
    return 0


def check_output(args, **kw):
    kw.pop("stdout", None)
    return run(args, stdout=PIPE, check=True, **kw).stdout


class _Pipe:
    """File-like end of a live child pipe (bytes or text)."""
    def __init__(self, fd, text, readable):
        self.fd = fd
        self.text = text
        self.readable = readable
        self.closed = False
        self._buf = b""

    def fileno(self):
        return self.fd

    def write(self, data):
        if isinstance(data, str):
            data = data.encode("utf-8")
        return _proc.write(self.fd, bytes(data))

    def flush(self):
        pass

    def read1(self, n=4096, timeout=None):
        """Read at most n bytes available now; None on timeout, b'' at end of file.
        timeout=None blocks (this is an extension: Popen.read_nonblock is the usual use)."""
        if self._buf:
            r, self._buf = self._buf[:n], self._buf[n:]
            return r
        return _proc.read(self.fd, n, timeout)

    def read(self, n=-1):
        out = b""
        while n < 0 or len(out) < n:
            d = self.read1(4096 if n < 0 else n - len(out))
            if not d:
                break
            out += d
        return out.decode("utf-8", "replace") if self.text else out

    def close(self):
        if not self.closed:
            self.closed = True
            _proc.close(self.fd)


class Popen:
    """Without stdin/stdout/stderr=PIPE the child runs in one step at communicate()/wait().
    With a pipe the child starts at once and stays alive: write to p.stdin, read with
    p.stdout.read1(n, timeout) or p.read_nonblock(timeout), p.send_signal / kill / terminate,
    p.poll() / wait(); communicate() writes the input, closes stdin and collects the rest."""
    def __init__(self, args, stdin=None, stdout=None, stderr=None, cwd=None, shell=False,
                 text=False, universal_newlines=False, encoding=None, **kw):
        self.args = args
        self.cwd = cwd
        self.shell = shell
        self.text = text or universal_newlines or encoding is not None
        self._stderr = stderr
        self._stdout = stdout
        self.returncode = None
        self._res = None
        self.stdin = self.stdout = self.stderr = None
        self.pid = None
        self._live = stdin == PIPE
        if self._live:
            if stdout != PIPE:
                raise ValueError("live Popen needs stdout=PIPE with stdin=PIPE")
            argv = _argv(args, shell)
            try:
                pid, fi, fo, fe = _proc.spawn(argv, cwd, 0 if stderr == PIPE else (1 if stderr == STDOUT else 2))
            except OSError:
                raise FileNotFoundError(2, "No such file or directory: '%s'" % argv[0])
            self.pid = pid
            self.stdin = _Pipe(fi, self.text, False)
            self.stdout = _Pipe(fo, self.text, True)
            if fe >= 0:
                self.stderr = _Pipe(fe, self.text, True)

    def read_nonblock(self, timeout=0, n=4096):
        """Data available on stdout within timeout seconds: bytes/str, '' at end of file, None if none."""
        d = self.stdout.read1(n, timeout)
        if d is not None and self.text:
            d = d.decode("utf-8", "replace")
        return d

    def send_signal(self, sig):
        if self._live and self.returncode is None:
            _proc.kill(self.pid, sig)

    def terminate(self):
        self.send_signal(15)

    def kill(self):
        self.send_signal(9)

    def _reap(self, nohang):
        if self.returncode is None:
            done, rc = _proc.wait(self.pid, nohang)
            if done:
                self.returncode = rc
        return self.returncode

    def communicate(self, input=None, timeout=None):
        if self._live:
            if input is not None and not self.stdin.closed:
                self.stdin.write(input)
            self.stdin.close()
            out = self.stdout.read()
            err = self.stderr.read() if self.stderr is not None else None
            self._reap(False)
            return (out, err)
        if self._res is None:
            rc, out, err = _exec(self.args, input, timeout, self.cwd, self.shell, self._stderr, self.text)
            self.returncode = rc
            self._res = (out if self._stdout == PIPE else None,
                         err if self._stderr == PIPE else None)
        return self._res

    def wait(self, timeout=None):
        if self._live:
            return self._reap(False)
        self.communicate(None, timeout)
        return self.returncode

    def poll(self):
        if self._live:
            return self._reap(True)
        if self._res is None:
            self.communicate()
        return self.returncode

    def __enter__(self):
        return self

    def __exit__(self, *a):
        if self._live:
            for f in (self.stdin, self.stdout, self.stderr):
                if f is not None:
                    f.close()
            self._reap(False)
