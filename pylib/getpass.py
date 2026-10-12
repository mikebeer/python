import os as _os
import sys as _sys


class GetPassWarning(UserWarning):
    pass


def getuser():
    for name in ('LOGNAME', 'USER', 'LNAME', 'USERNAME'):
        user = _os.getenv(name)
        if user:
            return user
    return 'user'


def getpass(prompt='Password: ', stream=None):
    return unix_getpass(prompt, stream)


def unix_getpass(prompt='Password: ', stream=None):
    if stream is None:
        stream = _sys.stderr
    stream.write(prompt)
    line = _sys.stdin.readline()
    if not line:
        raise EOFError
    return line.rstrip('\n')


win_getpass = fallback_getpass = unix_getpass
