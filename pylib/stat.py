ST_MODE = 0
ST_INO = 1
ST_DEV = 2
ST_NLINK = 3
ST_UID = 4
ST_GID = 5
ST_SIZE = 6
ST_ATIME = 7
ST_MTIME = 8
ST_CTIME = 9
S_IFDIR = 0o040000
S_IFCHR = 0o020000
S_IFBLK = 0o060000
S_IFREG = 0o100000
S_IFIFO = 0o010000
S_IFLNK = 0o120000
S_IFSOCK = 0o140000
S_ISUID = 0o4000
S_ISGID = 0o2000
S_ISVTX = 0o1000
S_IRWXU = 0o700
S_IRUSR = 0o400
S_IWUSR = 0o200
S_IXUSR = 0o100
S_IRWXG = 0o070
S_IRGRP = 0o040
S_IWGRP = 0o020
S_IXGRP = 0o010
S_IRWXO = 0o007
S_IROTH = 0o004
S_IWOTH = 0o002
S_IXOTH = 0o001


def S_IMODE(mode):
    return mode & 0o7777


def S_IFMT(mode):
    return mode & 0o170000


def S_ISDIR(mode):
    return S_IFMT(mode) == S_IFDIR


def S_ISCHR(mode):
    return S_IFMT(mode) == S_IFCHR


def S_ISBLK(mode):
    return S_IFMT(mode) == S_IFBLK


def S_ISREG(mode):
    return S_IFMT(mode) == S_IFREG


def S_ISFIFO(mode):
    return S_IFMT(mode) == S_IFIFO


def S_ISLNK(mode):
    return S_IFMT(mode) == S_IFLNK


def S_ISSOCK(mode):
    return S_IFMT(mode) == S_IFSOCK


def filemode(mode):
    t = {S_IFDIR: 'd', S_IFCHR: 'c', S_IFBLK: 'b', S_IFREG: '-', S_IFIFO: 'p', S_IFLNK: 'l', S_IFSOCK: 's'}.get(S_IFMT(mode), '?')
    bits = [(S_IRUSR, 'r'), (S_IWUSR, 'w'), (S_IXUSR, 'x'), (S_IRGRP, 'r'), (S_IWGRP, 'w'), (S_IXGRP, 'x'),
            (S_IROTH, 'r'), (S_IWOTH, 'w'), (S_IXOTH, 'x')]
    s = t
    for b, c in bits:
        s += c if mode & b else '-'
    return s
