# platform: static answers describing the interpreters
import sys as _sys
from collections import namedtuple as _nt

uname_result = _nt('uname_result', 'system node release version machine')


def system():
    return 'Linux'


def node():
    return 'localhost'


def release():
    return ''


def version():
    return ''


def machine():
    return 'x86_64'


def processor():
    return ''


def architecture(executable=None, bits='', linkage=''):
    return ('64bit', 'ELF')


def uname():
    return uname_result(system(), node(), release(), version(), machine())


def python_version():
    return '3.13.0'


def python_version_tuple():
    return ('3', '13', '0')


def python_implementation():
    return 'CPython'


def python_compiler():
    return 'GCC'


def python_branch():
    return ''


def python_revision():
    return ''


def python_build():
    return ('main', '')


def platform(aliased=False, terse=False):
    return 'Linux-' + release() + '-' + machine()


def libc_ver(executable=None, lib='', version='', chunksize=16384):
    return (lib, version)


def freedesktop_os_release():
    return {}
