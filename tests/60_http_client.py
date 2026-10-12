import http.client, http, urllib.request, urllib.error

class FakeSock:
    def __init__(self, data): self.data = data; self.sent = b''
    def recv(self, n):
        d = self.data[:n]; self.data = self.data[n:]; return d
    def sendall(self, b): self.sent += b
    def close(self): pass
    def makefile(self, mode): import io; return io.BytesIO(self.data)

r = http.client.HTTPResponse(FakeSock(b'HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/plain\r\nX-A: 1\r\n\r\nhello'))
r.begin()
print(r.status, r.reason, r.getheader('content-type'), r.getheader('missing', 'dflt'), r.read(), r.version)
r = http.client.HTTPResponse(FakeSock(b'HTTP/1.1 404 Not Found\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n'))
r.begin(); print(r.status, r.reason, r.read(), r.chunked)
print(http.client.responses[404], http.client.responses[200], http.HTTPStatus.NOT_FOUND.phrase, int(http.HTTPStatus.OK), http.HTTPStatus(418).phrase)
c = http.client.HTTPConnection('example.com:8080'); print(c.host, c.port)
c.sock = FakeSock(b'HTTP/1.0 204 No Content\r\n\r\n')
c.request('GET', '/x?y=1', headers={'A': 'b'})
print(c.sock.sent)
resp = c.getresponse(); print(resp.status, resp.read())
req = urllib.request.Request('http://h.test/p?q=1', data=b'x=1', headers={'content-type': 't'})
print(req.get_method(), req.full_url, req.selector, req.host, req.get_header('Content-type'))
try: raise urllib.error.HTTPError('u', 500, 'bad', {}, None)
except urllib.error.URLError as e: print(e, repr(e), e.code)
print(urllib.error.URLError('boom'))
try: http.client.HTTPResponse(FakeSock(b'')).begin()
except http.client.RemoteDisconnected as e: print('RemoteDisconnected')
