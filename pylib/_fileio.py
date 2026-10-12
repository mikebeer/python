# Best-effort binary file helpers: the interpreters' open(..., 'rb'/'wb') currently handles text only,
# so these fall back to str <-> bytes conversion (exact for valid UTF-8 data).


def read_bytes(path):
    f = open(path, 'rb')
    try:
        d = f.read()
    finally:
        f.close()
    if isinstance(d, str):
        d = d.encode('utf-8')
    return d


def write_bytes(path, data):
    data = bytes(data)
    f = open(path, 'wb')
    try:
        try:
            return f.write(data)
        except TypeError:
            pass
    finally:
        f.close()
    f = open(path, 'w')
    try:
        f.write(data.decode('utf-8'))
    finally:
        f.close()
    return len(data)
