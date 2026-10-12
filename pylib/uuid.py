# Pure Python uuid
import hashlib as _hashlib
import random as _random
import time as _time
from enum import Enum as _Enum

__all__ = ['UUID', 'uuid1', 'uuid3', 'uuid4', 'uuid5', 'NAMESPACE_DNS', 'NAMESPACE_URL', 'NAMESPACE_OID',
           'NAMESPACE_X500', 'getnode', 'SafeUUID', 'RESERVED_NCS', 'RFC_4122', 'RESERVED_MICROSOFT',
           'RESERVED_FUTURE']

RESERVED_NCS = 'reserved for NCS compatibility'
RFC_4122 = 'specified in RFC 4122'
RESERVED_MICROSOFT = 'reserved for Microsoft compatibility'
RESERVED_FUTURE = 'reserved for future definition'


class SafeUUID:
    safe = 0
    unsafe = -1
    unknown = None


class UUID:
    def __init__(self, hex=None, bytes=None, bytes_le=None, fields=None, int=None, version=None, *, is_safe=None):
        if [hex, bytes, bytes_le, fields, int].count(None) != 4:
            raise TypeError('one of the hex, bytes, bytes_le, fields, or int arguments must be given')
        if hex is not None:
            hex = hex.replace('urn:', '').replace('uuid:', '')
            hex = hex.strip('{}').replace('-', '')
            if len(hex) != 32:
                raise ValueError('badly formed hexadecimal UUID string')
            int = _int(hex, 16)
        if bytes_le is not None:
            if len(bytes_le) != 16:
                raise ValueError('bytes_le is not a 16-char string')
            b = _bytes(bytes_le)
            bytes = (b[4 - 1::-1] + b[6 - 1:4 - 1:-1] + b[8 - 1:6 - 1:-1] + b[8:])
        if bytes is not None:
            if len(bytes) != 16:
                raise ValueError('bytes is not a 16-char string')
            int = _int_from_bytes(bytes, 'big')
        if fields is not None:
            if len(fields) != 6:
                raise ValueError('fields is not a 6-tuple')
            (time_low, time_mid, time_hi_version, clock_seq_hi_variant, clock_seq_low, node) = fields
            if not 0 <= time_low < 1 << 32:
                raise ValueError('field 1 out of range (need a 32-bit value)')
            if not 0 <= time_mid < 1 << 16:
                raise ValueError('field 2 out of range (need a 16-bit value)')
            if not 0 <= time_hi_version < 1 << 16:
                raise ValueError('field 3 out of range (need a 16-bit value)')
            if not 0 <= clock_seq_hi_variant < 1 << 8:
                raise ValueError('field 4 out of range (need an 8-bit value)')
            if not 0 <= clock_seq_low < 1 << 8:
                raise ValueError('field 5 out of range (need an 8-bit value)')
            if not 0 <= node < 1 << 48:
                raise ValueError('field 6 out of range (need a 48-bit value)')
            clock_seq = (clock_seq_hi_variant << 8) | clock_seq_low
            int = ((time_low << 96) | (time_mid << 80) | (time_hi_version << 64) | (clock_seq << 48) | node)
        if int is not None:
            if not 0 <= int < 1 << 128:
                raise ValueError('int is out of range (need a 128-bit value)')
        if version is not None:
            if not 1 <= version <= 5:
                raise ValueError('illegal version number')
            int &= ~(0xc000 << 48)
            int |= 0x8000 << 48
            int &= ~(0xf000 << 64)
            int |= version << 76
        object.__setattr__(self, 'int', int)
        object.__setattr__(self, 'is_safe', SafeUUID.unknown if is_safe is None else is_safe)

    def __eq__(self, other):
        if isinstance(other, UUID):
            return self.int == other.int
        return NotImplemented

    def __ne__(self, other):
        if isinstance(other, UUID):
            return self.int != other.int
        return NotImplemented

    def __lt__(self, other):
        if isinstance(other, UUID):
            return self.int < other.int
        return NotImplemented

    def __gt__(self, other):
        if isinstance(other, UUID):
            return self.int > other.int
        return NotImplemented

    def __le__(self, other):
        if isinstance(other, UUID):
            return self.int <= other.int
        return NotImplemented

    def __ge__(self, other):
        if isinstance(other, UUID):
            return self.int >= other.int
        return NotImplemented

    def __hash__(self):
        return hash(self.int)

    def __int__(self):
        return self.int

    def __repr__(self):
        return '%s(%r)' % (self.__class__.__name__, str(self))

    def __str__(self):
        h = '%032x' % self.int
        return '%s-%s-%s-%s-%s' % (h[:8], h[8:12], h[12:16], h[16:20], h[20:])

    @property
    def bytes(self):
        return self.int.to_bytes(16, 'big')

    @property
    def bytes_le(self):
        b = self.bytes
        return (b[4 - 1::-1] + b[6 - 1:4 - 1:-1] + b[8 - 1:6 - 1:-1] + b[8:])

    @property
    def fields(self):
        return (self.time_low, self.time_mid, self.time_hi_version, self.clock_seq_hi_variant, self.clock_seq_low,
                self.node)

    @property
    def time_low(self):
        return self.int >> 96

    @property
    def time_mid(self):
        return (self.int >> 80) & 0xffff

    @property
    def time_hi_version(self):
        return (self.int >> 64) & 0xffff

    @property
    def clock_seq_hi_variant(self):
        return (self.int >> 56) & 0xff

    @property
    def clock_seq_low(self):
        return (self.int >> 48) & 0xff

    @property
    def time(self):
        return (((self.time_hi_version & 0x0fff) << 48) | (self.time_mid << 32) | self.time_low)

    @property
    def clock_seq(self):
        return (((self.clock_seq_hi_variant & 0x3f) << 8) | self.clock_seq_low)

    @property
    def node(self):
        return self.int & 0xffffffffffff

    @property
    def hex(self):
        return '%032x' % self.int

    @property
    def urn(self):
        return 'urn:uuid:' + str(self)

    @property
    def variant(self):
        if not self.int & (0x8000 << 48):
            return RESERVED_NCS
        elif not self.int & (0x4000 << 48):
            return RFC_4122
        elif not self.int & (0x2000 << 48):
            return RESERVED_MICROSOFT
        else:
            return RESERVED_FUTURE

    @property
    def version(self):
        if self.variant == RFC_4122:
            return int((self.int >> 76) & 0xf)
        return None

    def __setattr__(self, name, value):
        raise TypeError('UUID objects are immutable')


_int = int
_bytes = bytes
_int_from_bytes = int.from_bytes


def getnode():
    return 0x123456789abc


def uuid1(node=None, clock_seq=None):
    ns = _int(_time.time() * 1e9)
    timestamp = ns // 100 + 0x01b21dd213814000
    if clock_seq is None:
        clock_seq = _random.getrandbits(14)
    time_low = timestamp & 0xffffffff
    time_mid = (timestamp >> 32) & 0xffff
    time_hi_version = (timestamp >> 48) & 0x0fff
    clock_seq_low = clock_seq & 0xff
    clock_seq_hi_variant = (clock_seq >> 8) & 0x3f
    if node is None:
        node = getnode()
    return UUID(fields=(time_low, time_mid, time_hi_version, clock_seq_hi_variant, clock_seq_low, node), version=1)


def uuid3(namespace, name):
    if isinstance(name, str):
        name = name.encode()
    h = _hashlib.md5(namespace.bytes + name).digest()
    return UUID(bytes=h[:16], version=3)


def uuid4():
    return UUID(int=_random.getrandbits(128), version=4)


def uuid5(namespace, name):
    if isinstance(name, str):
        name = name.encode()
    h = _hashlib.sha1(namespace.bytes + name).digest()
    return UUID(bytes=h[:16], version=5)


NAMESPACE_DNS = UUID('6ba7b810-9dad-11d1-80b4-00c04fd430c8')
NAMESPACE_URL = UUID('6ba7b811-9dad-11d1-80b4-00c04fd430c8')
NAMESPACE_OID = UUID('6ba7b812-9dad-11d1-80b4-00c04fd430c8')
NAMESPACE_X500 = UUID('6ba7b814-9dad-11d1-80b4-00c04fd430c8')
NIL = UUID('00000000-0000-0000-0000-000000000000')
MAX = UUID('ffffffff-ffff-ffff-ffff-ffffffffffff')
