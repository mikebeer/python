# Pure Python hashlib: md5 sha1 sha224 sha256 sha384 sha512 sha3_* shake_* + pbkdf2_hmac, scrypt-free.
algorithms_guaranteed = {'md5', 'sha1', 'sha224', 'sha256', 'sha384', 'sha512',
                         'sha3_224', 'sha3_256', 'sha3_384', 'sha3_512', 'shake_128', 'shake_256'}
algorithms_available = algorithms_guaranteed


def _tob(data):
    if isinstance(data, str):
        raise TypeError("Strings must be encoded before hashing")
    if isinstance(data, (bytes, bytearray)):
        return bytes(data)
    if hasattr(data, 'tobytes'):
        return data.tobytes()
    raise TypeError("object supporting the buffer API required")


def _rol(x, n):
    return ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF


def _ror(x, n):
    return ((x >> n) | (x << (32 - n))) & 0xFFFFFFFF


class _Hash:
    # generic Merkle-Damgard buffering
    name = ''
    digest_size = 0
    block_size = 64
    _lenbytes = 8
    _big = True

    def __init__(self, data=b''):
        self._buf = b''
        self._len = 0
        self._init()
        if data:
            self.update(data)

    def update(self, data):
        data = _tob(data)
        self._len += len(data)
        buf = self._buf + data
        bs = self.block_size
        n = len(buf) // bs
        i = 0
        while i < n:
            self._block(buf[i * bs:(i + 1) * bs])
            i += 1
        self._buf = buf[n * bs:]

    def _finish(self):
        bs = self.block_size
        lb = self._lenbytes
        bits = self._len * 8
        pad = b'\x80' + b'\x00' * ((bs - lb - 1 - self._len) % bs)
        if self._big:
            pad += bits.to_bytes(lb, 'big')
        else:
            pad += bits.to_bytes(lb, 'little')
        buf = self._buf + pad
        n = len(buf) // bs
        i = 0
        while i < n:
            self._block(buf[i * bs:(i + 1) * bs])
            i += 1

    def copy(self):
        h = self.__class__()
        h._buf = self._buf
        h._len = self._len
        h._h = list(self._h)
        return h

    def digest(self):
        c = self.copy()
        c._finish()
        return c._out()

    def hexdigest(self):
        return self.digest().hex()

    def __repr__(self):
        return '<%s _hashlib.HASH object @ 0x7f0000000000>' % self.name


_K256 = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2]


class _MD5(_Hash):
    name = 'md5'
    digest_size = 16
    _big = False

    def _init(self):
        self._h = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476]

    def _block(self, blk):
        M = [int.from_bytes(blk[i * 4:i * 4 + 4], 'little') for i in range(16)]
        a, b, c, d = self._h
        for i in range(64):
            if i < 16:
                f = (b & c) | (~b & d)
                g = i
            elif i < 32:
                f = (d & b) | (~d & c)
                g = (5 * i + 1) % 16
            elif i < 48:
                f = b ^ c ^ d
                g = (3 * i + 5) % 16
            else:
                f = c ^ (b | (~d & 0xFFFFFFFF))
                g = (7 * i) % 16
            f = (f + a + _MD5K[i] + M[g]) & 0xFFFFFFFF
            a = d
            d = c
            c = b
            b = (b + _rol(f, _MD5S[i])) & 0xFFFFFFFF
        h = self._h
        h[0] = (h[0] + a) & 0xFFFFFFFF
        h[1] = (h[1] + b) & 0xFFFFFFFF
        h[2] = (h[2] + c) & 0xFFFFFFFF
        h[3] = (h[3] + d) & 0xFFFFFFFF

    def _out(self):
        return b''.join([x.to_bytes(4, 'little') for x in self._h])


_MD5S = [7, 12, 17, 22] * 4 + [5, 9, 14, 20] * 4 + [4, 11, 16, 23] * 4 + [6, 10, 15, 21] * 4
_MD5K = [0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
         0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
         0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
         0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
         0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
         0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
         0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
         0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391]


class _SHA1(_Hash):
    name = 'sha1'
    digest_size = 20

    def _init(self):
        self._h = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0]

    def _block(self, blk):
        w = [int.from_bytes(blk[i * 4:i * 4 + 4], 'big') for i in range(16)]
        for i in range(16, 80):
            x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]
            w.append(((x << 1) | (x >> 31)) & 0xFFFFFFFF)
        a, b, c, d, e = self._h
        for i in range(80):
            if i < 20:
                f = (b & c) | (~b & d)
                k = 0x5A827999
            elif i < 40:
                f = b ^ c ^ d
                k = 0x6ED9EBA1
            elif i < 60:
                f = (b & c) | (b & d) | (c & d)
                k = 0x8F1BBCDC
            else:
                f = b ^ c ^ d
                k = 0xCA62C1D6
            t = (((a << 5) | (a >> 27)) + (f & 0xFFFFFFFF) + e + k + w[i]) & 0xFFFFFFFF
            e = d
            d = c
            c = ((b << 30) | (b >> 2)) & 0xFFFFFFFF
            b = a
            a = t
        h = self._h
        h[0] = (h[0] + a) & 0xFFFFFFFF
        h[1] = (h[1] + b) & 0xFFFFFFFF
        h[2] = (h[2] + c) & 0xFFFFFFFF
        h[3] = (h[3] + d) & 0xFFFFFFFF
        h[4] = (h[4] + e) & 0xFFFFFFFF

    def _out(self):
        return b''.join([x.to_bytes(4, 'big') for x in self._h])


class _SHA256(_Hash):
    name = 'sha256'
    digest_size = 32
    _IV = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]

    def _init(self):
        self._h = list(self._IV)

    def _block(self, blk):
        w = [int.from_bytes(blk[i * 4:i * 4 + 4], 'big') for i in range(16)]
        for i in range(16, 64):
            x = w[i - 15]
            y = w[i - 2]
            s0 = (((x >> 7) | (x << 25)) ^ ((x >> 18) | (x << 14)) ^ (x >> 3)) & 0xFFFFFFFF
            s1 = (((y >> 17) | (y << 15)) ^ ((y >> 19) | (y << 13)) ^ (y >> 10)) & 0xFFFFFFFF
            w.append((w[i - 16] + s0 + w[i - 7] + s1) & 0xFFFFFFFF)
        a, b, c, d, e, f, g, h = self._h
        for i in range(64):
            S1 = (((e >> 6) | (e << 26)) ^ ((e >> 11) | (e << 21)) ^ ((e >> 25) | (e << 7))) & 0xFFFFFFFF
            ch = (e & f) ^ (~e & g)
            t1 = (h + S1 + (ch & 0xFFFFFFFF) + _K256[i] + w[i]) & 0xFFFFFFFF
            S0 = (((a >> 2) | (a << 30)) ^ ((a >> 13) | (a << 19)) ^ ((a >> 22) | (a << 10))) & 0xFFFFFFFF
            maj = (a & b) ^ (a & c) ^ (b & c)
            t2 = (S0 + maj) & 0xFFFFFFFF
            h = g
            g = f
            f = e
            e = (d + t1) & 0xFFFFFFFF
            d = c
            c = b
            b = a
            a = (t1 + t2) & 0xFFFFFFFF
        s = self._h
        s[0] = (s[0] + a) & 0xFFFFFFFF
        s[1] = (s[1] + b) & 0xFFFFFFFF
        s[2] = (s[2] + c) & 0xFFFFFFFF
        s[3] = (s[3] + d) & 0xFFFFFFFF
        s[4] = (s[4] + e) & 0xFFFFFFFF
        s[5] = (s[5] + f) & 0xFFFFFFFF
        s[6] = (s[6] + g) & 0xFFFFFFFF
        s[7] = (s[7] + h) & 0xFFFFFFFF

    def _out(self):
        return b''.join([x.to_bytes(4, 'big') for x in self._h])[:self.digest_size]


class _SHA224(_SHA256):
    name = 'sha224'
    digest_size = 28
    _IV = [0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4]


_K512 = [
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538,
    0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe,
    0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2, 0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
    0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
    0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5, 0x983e5152ee66dfab,
    0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
    0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed,
    0x53380d139d95b3df, 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
    0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
    0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8, 0x19a4c116b8d2d0c8, 0x1e376c085141ab53,
    0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373,
    0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c,
    0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6,
    0x113f9804bef90dae, 0x1b710b35131c471b, 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
    0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817]
_M64 = 0xFFFFFFFFFFFFFFFF


class _SHA512(_Hash):
    name = 'sha512'
    digest_size = 64
    block_size = 128
    _lenbytes = 16
    _IV = [0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,
           0x510e527fade682d1, 0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179]

    def _init(self):
        self._h = list(self._IV)

    def _block(self, blk):
        w = [int.from_bytes(blk[i * 8:i * 8 + 8], 'big') for i in range(16)]
        for i in range(16, 80):
            x = w[i - 15]
            y = w[i - 2]
            s0 = ((x >> 1) | (x << 63)) ^ ((x >> 8) | (x << 56)) ^ (x >> 7)
            s1 = ((y >> 19) | (y << 45)) ^ ((y >> 61) | (y << 3)) ^ (y >> 6)
            w.append((w[i - 16] + (s0 & _M64) + w[i - 7] + (s1 & _M64)) & _M64)
        a, b, c, d, e, f, g, h = self._h
        for i in range(80):
            S1 = ((e >> 14) | (e << 50)) ^ ((e >> 18) | (e << 46)) ^ ((e >> 41) | (e << 23))
            ch = (e & f) ^ (~e & g)
            t1 = (h + (S1 & _M64) + (ch & _M64) + _K512[i] + w[i]) & _M64
            S0 = ((a >> 28) | (a << 36)) ^ ((a >> 34) | (a << 30)) ^ ((a >> 39) | (a << 25))
            maj = (a & b) ^ (a & c) ^ (b & c)
            t2 = ((S0 & _M64) + maj) & _M64
            h = g
            g = f
            f = e
            e = (d + t1) & _M64
            d = c
            c = b
            b = a
            a = (t1 + t2) & _M64
        s = self._h
        s[0] = (s[0] + a) & _M64
        s[1] = (s[1] + b) & _M64
        s[2] = (s[2] + c) & _M64
        s[3] = (s[3] + d) & _M64
        s[4] = (s[4] + e) & _M64
        s[5] = (s[5] + f) & _M64
        s[6] = (s[6] + g) & _M64
        s[7] = (s[7] + h) & _M64

    def _out(self):
        return b''.join([x.to_bytes(8, 'big') for x in self._h])[:self.digest_size]


class _SHA384(_SHA512):
    name = 'sha384'
    digest_size = 48
    _IV = [0xcbbb9d5dc1059ed8, 0x629a292a367cd507, 0x9159015a3070dd17, 0x152fecd8f70e5939,
           0x67332667ffc00b31, 0x8eb44a8768581511, 0xdb0c2e0d64f98fa7, 0x47b5481dbefa4fa4]


# ---- SHA-3 / Keccak
_RC = [0x0000000000000001, 0x0000000000008082, 0x800000000000808a, 0x8000000080008000, 0x000000000000808b,
       0x0000000080000001, 0x8000000080008081, 0x8000000000008009, 0x000000000000008a, 0x0000000000000088,
       0x0000000080008009, 0x000000008000000a, 0x000000008000808b, 0x800000000000008b, 0x8000000000008089,
       0x8000000000008003, 0x8000000000008002, 0x8000000000000080, 0x000000000000800a, 0x800000008000000a,
       0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008]
_ROT = [[0, 36, 3, 41, 18], [1, 44, 10, 45, 2], [62, 6, 43, 15, 61], [28, 55, 25, 21, 56], [27, 20, 39, 8, 14]]


def _keccak_f(A):
    for rnd in range(24):
        C = [A[x][0] ^ A[x][1] ^ A[x][2] ^ A[x][3] ^ A[x][4] for x in range(5)]
        D = [C[(x - 1) % 5] ^ (((C[(x + 1) % 5] << 1) | (C[(x + 1) % 5] >> 63)) & _M64) for x in range(5)]
        for x in range(5):
            for y in range(5):
                A[x][y] ^= D[x]
        B = [[0] * 5 for _ in range(5)]
        for x in range(5):
            for y in range(5):
                v = A[x][y]
                r = _ROT[x][y]
                if r:
                    v = ((v << r) | (v >> (64 - r))) & _M64
                B[y][(2 * x + 3 * y) % 5] = v
        for x in range(5):
            for y in range(5):
                A[x][y] = B[x][y] ^ ((~B[(x + 1) % 5][y] & _M64) & B[(x + 2) % 5][y])
        A[0][0] ^= _RC[rnd]


class _Sha3:
    def __init__(self, data=b''):
        self._A = [[0] * 5 for _ in range(5)]
        self._buf = b''
        if data:
            self.update(data)

    def _absorb(self, blk):
        A = self._A
        for i in range(len(blk) // 8):
            A[i % 5][i // 5] ^= int.from_bytes(blk[i * 8:i * 8 + 8], 'little')
        _keccak_f(A)

    def update(self, data):
        buf = self._buf + _tob(data)
        r = self.block_size
        n = len(buf) // r
        for i in range(n):
            self._absorb(buf[i * r:(i + 1) * r])
        self._buf = buf[n * r:]

    def copy(self):
        h = self.__class__()
        h._buf = self._buf
        h._A = [list(c) for c in self._A]
        return h

    def _squeeze(self, n):
        c = self.copy()
        r = self.block_size
        buf = c._buf
        pad = bytearray(buf) + bytearray(r - len(buf))
        pad[len(buf)] ^= self._suffix
        pad[r - 1] ^= 0x80
        c._absorb(bytes(pad))
        out = b''
        while True:
            A = c._A
            blk = b''.join([A[i % 5][i // 5].to_bytes(8, 'little') for i in range(r // 8)])
            out += blk
            if len(out) >= n:
                return out[:n]
            _keccak_f(A)

    def digest(self):
        return self._squeeze(self.digest_size)

    def hexdigest(self):
        return self.digest().hex()

    def __repr__(self):
        return '<%s _hashlib.HASH object @ 0x7f0000000000>' % self.name


class _Sha3_224(_Sha3):
    name = 'sha3_224'
    digest_size = 28
    block_size = 144
    _suffix = 6


class _Sha3_256(_Sha3):
    name = 'sha3_256'
    digest_size = 32
    block_size = 136
    _suffix = 6


class _Sha3_384(_Sha3):
    name = 'sha3_384'
    digest_size = 48
    block_size = 104
    _suffix = 6


class _Sha3_512(_Sha3):
    name = 'sha3_512'
    digest_size = 64
    block_size = 72
    _suffix = 6


class _Shake(_Sha3):
    digest_size = 0
    _suffix = 0x1f

    def digest(self, length):
        return self._squeeze(length)

    def hexdigest(self, length):
        return self._squeeze(length).hex()


class _Shake128(_Shake):
    name = 'shake_128'
    block_size = 168


class _Shake256(_Shake):
    name = 'shake_256'
    block_size = 136


def md5(data=b'', usedforsecurity=True):
    return _MD5(data)


def sha1(data=b'', usedforsecurity=True):
    return _SHA1(data)


def sha224(data=b'', usedforsecurity=True):
    return _SHA224(data)


def sha256(data=b'', usedforsecurity=True):
    return _SHA256(data)


def sha384(data=b'', usedforsecurity=True):
    return _SHA384(data)


def sha512(data=b'', usedforsecurity=True):
    return _SHA512(data)


def sha3_224(data=b'', usedforsecurity=True):
    return _Sha3_224(data)


def sha3_256(data=b'', usedforsecurity=True):
    return _Sha3_256(data)


def sha3_384(data=b'', usedforsecurity=True):
    return _Sha3_384(data)


def sha3_512(data=b'', usedforsecurity=True):
    return _Sha3_512(data)


def shake_128(data=b'', usedforsecurity=True):
    return _Shake128(data)


def shake_256(data=b'', usedforsecurity=True):
    return _Shake256(data)


_TABLE = {'md5': md5, 'sha1': sha1, 'sha224': sha224, 'sha256': sha256, 'sha384': sha384, 'sha512': sha512,
          'sha3_224': sha3_224, 'sha3_256': sha3_256, 'sha3_384': sha3_384, 'sha3_512': sha3_512,
          'shake_128': shake_128, 'shake_256': shake_256}


def new(name, data=b'', usedforsecurity=True):
    n = name.lower().replace('-', '_') if isinstance(name, str) else name
    if n in _TABLE:
        return _TABLE[n](data)
    if n == 'sha_1':
        return sha1(data)
    raise ValueError('unsupported hash type ' + str(name))


def file_digest(fileobj, digest, *, _bufsize=262144):
    h = digest() if callable(digest) else new(digest)
    while True:
        b = fileobj.read(_bufsize)
        if not b:
            break
        h.update(b)
    return h


def pbkdf2_hmac(hash_name, password, salt, iterations, dklen=None):
    import hmac as _hmac
    password = _tob(password)
    salt = _tob(salt)
    h0 = new(hash_name)
    if dklen is None:
        dklen = h0.digest_size
    bs = h0.block_size
    if len(password) > bs:
        password = new(hash_name, password).digest()
    password = password + b'\x00' * (bs - len(password))
    ik = h0.__class__(bytes([b ^ 0x36 for b in password]))
    ok = h0.__class__(bytes([b ^ 0x5c for b in password]))
    out = b''
    blk = 1
    while len(out) < dklen:
        h = ik.copy()
        h.update(salt + blk.to_bytes(4, 'big'))
        o = ok.copy()
        o.update(h.digest())
        u = o.digest()
        t = int.from_bytes(u, 'big')
        for _ in range(iterations - 1):
            h = ik.copy()
            h.update(u)
            o = ok.copy()
            o.update(h.digest())
            u = o.digest()
            t ^= int.from_bytes(u, 'big')
        out += t.to_bytes(len(u), 'big')
        blk += 1
    return out[:dklen]
