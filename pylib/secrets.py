# secrets: random data from the (non cryptographic) clock-seeded generator when no OS entropy is available
import pyrandom as _random
import base64 as _base64
import binascii as _binascii
from hmac import compare_digest

_sysrand = _random.SystemRandom()
randbelow = _sysrand._randbelow
choice = _sysrand.choice
randbits = _sysrand.getrandbits
DEFAULT_ENTROPY = 32


class SystemRandom(_random.SystemRandom):
    pass


def token_bytes(nbytes=None):
    if nbytes is None:
        nbytes = DEFAULT_ENTROPY
    return _sysrand.randbytes(nbytes)


def token_hex(nbytes=None):
    return _binascii.hexlify(token_bytes(nbytes)).decode('ascii')


def token_urlsafe(nbytes=None):
    tok = token_bytes(nbytes)
    return _base64.urlsafe_b64encode(tok).rstrip(b'=').decode('ascii')
