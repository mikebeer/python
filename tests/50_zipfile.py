import zipfile, io
buf = io.BytesIO()
with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('a.txt', 'hello hello hello hello hello')
    z.writestr('dir/b.bin', bytes(range(200)))
    z.writestr('empty.txt', '')
    z.writestr(zipfile.ZipInfo('stored.txt'), 'stored text', compress_type=zipfile.ZIP_STORED)
data = buf.getvalue()
print(data[:4], zipfile.is_zipfile(io.BytesIO(data)), zipfile.is_zipfile(io.BytesIO(b'nope')))
with zipfile.ZipFile(io.BytesIO(data)) as z:
    print(z.namelist(), z.testzip())
    for i in z.infolist():
        print(i.filename, i.file_size, i.compress_type, i.date_time[0] >= 1980, i.is_dir(), hex(i.CRC))
    print(z.read('a.txt'), z.read('dir/b.bin')[:5], z.read('empty.txt'), z.getinfo('stored.txt').file_size)
    with z.open('a.txt') as f:
        print(f.read(5))
    try:
        z.read('missing')
    except KeyError as e:
        print('KeyError', e)
# member data written by CPython 3.13 (stored + deflated), including central directory
cpy = bytes.fromhex('504b030414000000080001824c5daa20170a110000001800000005000000612e747874cb48cdc9c95728cf2fca49e1ca40620300504b030414000000080001824c5d8316dc8c030000000100000005000000622e747874ab0000504b0102140314000000080001824c5daa20170a1100000018000000050000000000000000000000800100000000612e747874504b0102140314000000080001824c5d8316dc8c0300000001000000050000000000000000000000800134000000622e747874504b05060000000002000200660000005a0000000000')
try:
    z = zipfile.ZipFile(io.BytesIO(cpy))
    print(z.namelist(), z.read('a.txt'), z.read('b.txt'))
except zipfile.BadZipFile as e:
    print('BadZipFile', e)
try:
    zipfile.ZipFile(io.BytesIO(b'garbage'))
except zipfile.BadZipFile as e:
    print('BadZipFile', e)
# corrupt crc
bad = bytearray(data)
bad[40] ^= 1
try:
    with zipfile.ZipFile(io.BytesIO(bytes(bad))) as z:
        print(z.testzip())
except Exception as e:
    print(type(e).__name__)
