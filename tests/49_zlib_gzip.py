import zlib, gzip, io, binascii
print(zlib.crc32(b'hello world'), zlib.crc32(b''), zlib.crc32(b'world', zlib.crc32(b'hello ')), zlib.adler32(b'hello world'), zlib.adler32(b''), zlib.adler32(b'x' * 10000), zlib.adler32(b'b', zlib.adler32(b'a')))
# data compressed by CPython zlib (dynamic Huffman, fixed Huffman, stored)
print(zlib.decompress(bytes.fromhex('789ccb48cdc9c95728cf2fca4951c840b00169e708d9')))
print(zlib.decompress(bytes.fromhex('78daedccd10d83300c84e1556e80aa3b99d84286380e892310d3375297a0529feffe2f7b1383d63e0cecd91bba06c8245e485ebaa490180dc45ab5272d2b24cf47fe778fe8baf0fc437474734688d5d96a49cacaa304c6b469993a24beb2c0682d04ca7a0c7a3f84a045b6e3de2fe3b379f176b25dfb7d6cb2fcd6f201c0364792'))[:30], len(zlib.decompress(bytes.fromhex('78daedccd10d83300c84e1556e80aa3b99d84286380e892310d3375297a0529feffe2f7b1383d63e0cecd91bba06c8245e485ebaa490180dc45ab5272d2b24cf47fe778fe8baf0fc437474734688d5d96a49cacaa304c6b469993a24beb2c0682d04ca7a0c7a3f84a045b6e3de2fe3b379f176b25dfb7d6cb2fcd6f201c0364792'))), zlib.decompress(bytes.fromhex('7801010003fffc000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeffa0627e90')) == bytes(range(256)) * 3)
print(zlib.decompress(zlib.compress(b'', 0)), zlib.decompress(zlib.compress(b'a')))
samples = [b'', b'a', b'abc' * 100, bytes(range(256)) * 5, b'hello hello hello hello', (b'The quick brown fox jumps over the lazy dog. ' * 40), bytes([i * 7 % 251 for i in range(3000)])]
for s in samples:
    for lv in (0, 1, 6, 9, -1):
        c = zlib.compress(s, lv)
        assert zlib.decompress(c) == s, (len(s), lv)
    assert zlib.decompress(zlib.compress(s, 5, -15), -15) == s
    assert zlib.decompress(zlib.compress(s, 5, 31), 31) == s
    assert zlib.decompress(zlib.compress(s), 47) == s
    assert len(zlib.compress(s, 9)) <= len(zlib.compress(s, 0)) + 6
print('roundtrips ok', len(zlib.compress(b'abc' * 100)) < 60, zlib.compress(b'abc', 0)[:2].hex())
co = zlib.compressobj(6)
d = co.compress(b'part one ') + co.compress(b'part two') + co.flush()
do = zlib.decompressobj()
print(do.decompress(d[:10]) + do.decompress(d[10:]), do.eof, do.unused_data)
do = zlib.decompressobj()
print(do.decompress(d + b'tail'), do.unused_data)
for bad in [b'', b'garbage!', zlib.compress(b'abc')[:-3], zlib.compress(b'abc')[:-1] + b'\x00']:
    try:
        zlib.decompress(bad)
        print('no error')
    except zlib.error as e:
        print('zlib.error', str(e)[:60])
try:
    zlib.compress(b'x', 12)
except zlib.error as e:
    print('zlib.error', e)
g = gzip.compress(b'hello gzip' * 5)
print(g[:3].hex(), gzip.decompress(g), gzip.decompress(bytes.fromhex('1f8b08000000000002ffcb48cdc9c95748afca2c50c8803301ac5ff12715000000')))
buf = io.BytesIO()
with gzip.GzipFile(fileobj=buf, mode='wb') as f:
    f.write(b'line1\nline2\n')
buf.seek(0)
with gzip.GzipFile(fileobj=buf, mode='rb') as f:
    print(f.readline(), f.read())
try:
    gzip.decompress(b'notgzip')
except gzip.BadGzipFile as e:
    print('BadGzipFile', e)
b = io.BytesIO(b'ab\ncd')
print(b.read(1), b.readline(), b.tell(), b.getvalue(), b.write(b'XY'), b.getvalue(), b.seek(0), list(b))
