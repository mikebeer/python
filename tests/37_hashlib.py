import hashlib, hmac
msgs = [b'', b'abc', b'The quick brown fox jumps over the lazy dog', b'x' * 200]
for name in ['md5', 'sha1', 'sha224', 'sha256', 'sha384', 'sha512', 'sha3_256', 'sha3_512']:
    for m in msgs:
        print(name, hashlib.new(name, m).hexdigest())
h = hashlib.sha256()
h.update(b'hello ')
h.update(b'world')
c = h.copy()
c.update(b'!')
print(h.hexdigest(), c.hexdigest(), h.digest_size, h.block_size, h.name)
print(hashlib.md5(b'abc').digest())
print(hashlib.sha3_224(b'abc').hexdigest(), hashlib.sha3_384(b'abc').hexdigest())
print(hashlib.shake_128(b'abc').hexdigest(20), hashlib.shake_256(b'abc').hexdigest(40))
print(hashlib.pbkdf2_hmac('sha256', b'password', b'salt', 3, 40).hex())
print(hashlib.pbkdf2_hmac('sha1', b'password', b'salt', 2).hex())
print(hmac.new(b'key', b'msg', hashlib.sha256).hexdigest())
print(hmac.new(b'k' * 100, b'msg', 'md5').hexdigest())
print(hmac.digest(b'key', b'msg', 'sha1').hex(), hmac.compare_digest(b'ab', b'ab'), hmac.compare_digest(b'ab', b'ac'))
try:
    hashlib.sha256('abc')
except TypeError as e:
    print('TypeError', e)
try:
    hashlib.new('nope')
except ValueError as e:
    print('ValueError', e)
hh = hmac.new(b'key', b'a', 'sha256'); h2 = hh.copy(); h2.update(b'b'); print(hh.hexdigest()[:16], h2.hexdigest()[:16])
