"""Minimal subprocess on top of the native _proc.run (POSIX builds only).

Without stdout/stderr/input/capture_output the child inherits the terminal (editors, pagers).
run / call / check_call / check_output / Popen(...).communicate().  A child is
always run to completion in one step; Popen defers the start until
communicate(), wait() or poll() (no interactive pipes).  Not supported:
shell=False PATH-less lookup tricks, preexec_fn, streaming reads.
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


class Popen:
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

    def communicate(self, input=None, timeout=None):
        if self._res is None:
            rc, out, err = _exec(self.args, input, timeout, self.cwd, self.shell, self._stderr, self.text)
            self.returncode = rc
            self._res = (out if self._stdout == PIPE else None,
                         err if self._stderr == PIPE else None)
        return self._res

    def wait(self, timeout=None):
        self.communicate(None, timeout)
        return self.returncode

    def poll(self):
        if self._res is None:
            self.communicate()
        return self.returncode
