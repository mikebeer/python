from urllib.parse import *
import urllib.parse
urls = ['http://user:pw@Example.com:8080/a/b;p=1/c.html?x=1&y=2#frag', 'https://example.com', '/relative/path?q=1', 'mailto:a@b.c',
        'file:///etc/hosts', 'http://[::1]:80/x', '//host/path', 'a/b/c', 'http://h/p?a=1;b=2', 'ftp://ftp.x.org/pub/file;type=a']
for u in urls:
    p = urlparse(u)
    print(p)
    print(p.scheme, p.netloc, p.path, p.params, p.query, p.fragment, p.hostname, p.port if u != 'x' else 0, p.username, p.password)
    print(urlsplit(u), urlunparse(p) == u, p.geturl())
print(urlparse(b'http://a/b?c=d'))
print(urljoin('http://a/b/c/d;p?q', 'g'), urljoin('http://a/b/c/d;p?q', '../g'), urljoin('http://a/b/c/d;p?q', '/g'),
      urljoin('http://a/b/c/d;p?q', '//g'), urljoin('http://a/b/c/d;p?q', '?y'), urljoin('http://a/b/c/d;p?q', '#s'),
      urljoin('http://a/b/c/d;p?q', '../../../g'), urljoin('http://a/b/c/d;p?q', 'http://x/y'), urljoin('http://a/b/', 'c/./d/../e'))
print(urldefrag('http://a/b#c'), urldefrag('http://a/b'))
print(quote('/a b/ü?&=~'), quote('a b', safe=''), quote_plus('a b&c=d/e'), quote(b'\xff\x00'), quote('x y', safe=' '))
print(unquote('a%20b%C3%BC%zz%4'), unquote_plus('a+b%2Bc'), unquote_to_bytes('%ff%41'), unquote(b'%41'))
print(urlencode({'a': 1, 'b': 'x y', 'c': 'ü'}), urlencode([('a', [1, 2]), ('b', 'z')], doseq=True), urlencode([('a', [1, 2])]))
print(parse_qs('a=1&b=2&a=3&c=&d'), parse_qs('a=1&c=', keep_blank_values=True), parse_qsl('x=a+b&y=%41;z=1', separator='&'))
print(parse_qs('a=1;b=2', separator=';'))
r = urlparse('http://x.com/p?q=1')
print(r._replace(path='/new'), r._replace(scheme='https').geturl(), r.encode())
try:
    urlparse('http://[::1/x')
except ValueError as e:
    print('ValueError', e)
try:
    print(urlparse('http://h:99999/').port)
except ValueError as e:
    print('ValueError', e)
print(urlunsplit(('http', 'h', 'p', 'q=1', 'f')), urlunparse(('', '', 'x', '', '', '')))
