import base64, binascii
samples = [b'', b'f', b'fo', b'foo', b'foob', b'fooba', b'foobar', bytes(range(256)), b'\xff\xfe\xfd\x00\x00\x00\x00 ']
for s in samples:
    print(base64.b64encode(s)[:60], base64.b32encode(s)[:60], base64.b16encode(s)[:40])
    assert base64.b64decode(base64.b64encode(s)) == s
    assert base64.b32decode(base64.b32encode(s)) == s
    assert base64.b16decode(base64.b16encode(s)) == s
    print(base64.a85encode(s)[:50], base64.b85encode(s)[:50], base64.urlsafe_b64encode(s)[:50])
    assert base64.a85decode(base64.a85encode(s)) == s
    assert base64.b85decode(base64.b85encode(s)) == s
    assert base64.urlsafe_b64decode(base64.urlsafe_b64encode(s)) == s
    assert base64.a85decode(base64.a85encode(s, adobe=True), adobe=True) == s
print(base64.b32hexencode(b'foobar'), base64.b32hexdecode(b'CPNMUOJ1E8======'))
print(base64.b64decode('Zm9vYmFy'), base64.b64decode(b'Zm9v\nYmFy'), base64.b64decode('Zm9vYg'+'=='))
print(base64.encodebytes(b'x' * 100))
print(base64.decodebytes(base64.encodebytes(b'hello world' * 10)))
print(base64.b64encode(b'\xfb\xff', altchars=b'-_'), base64.b64decode(b'-_8=', altchars=b'-_'))
print(base64.b32decode(b'mfrggzdf', casefold=True), base64.b16decode(b'6a6b', casefold=True))
print(base64.a85encode(b'hello world', wrapcol=8), base64.a85encode(b'\x00\x00\x00\x00  ', foldspaces=True))
for bad in [lambda: base64.b64decode('abc'), lambda: base64.b16decode(b'zz'), lambda: base64.b32decode(b'ABC'), lambda: base64.b64encode('str')]:
    try:
        bad()
    except Exception as e:
        print(type(e).__name__, e)
print(binascii.hexlify(b'\x01\xab'), binascii.unhexlify('01ab'), binascii.b2a_hex(b'xyz'), binascii.hexlify(b'abcd', '-', 2))
print(binascii.crc32(b'hello'), binascii.crc32(b'world', binascii.crc32(b'hello ')), binascii.crc32(b''), binascii.crc_hqx(b'hello', 0))
print(binascii.b2a_base64(b'hi there'), binascii.a2b_base64(b'aGkgdGhlcmU='), binascii.b2a_base64(b'x', newline=False))
print(binascii.b2a_uu(b'hello world'), binascii.a2b_uu(binascii.b2a_uu(b'hello world')))
try:
    binascii.unhexlify('abc')
except binascii.Error as e:
    print('Error', e)
try:
    binascii.unhexlify('zz')
except binascii.Error as e:
    print('Error', e)
