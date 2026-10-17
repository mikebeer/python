import subprocess, time, select, os

def rd(p, t):
    # portable "read what is there within t seconds": None if nothing, '' at end, else text
    if hasattr(p, "read_nonblock"):
        return p.read_nonblock(t)
    if not select.select([p.stdout.fileno()], [], [], t)[0]:
        return None
    return os.read(p.stdout.fileno(), 4096).decode()

p = subprocess.Popen(["cat"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
print(rd(p, 0.1))
p.stdin.write("one\n"); p.stdin.flush()
time.sleep(0.1)
print(repr(rd(p, 1)))
p.stdin.write("two\n"); p.stdin.flush()
time.sleep(0.1)
print(repr(rd(p, 1)))
print(p.poll())
p.stdin.close()
print(repr(rd(p, 1)))
print(p.wait())

# line-oriented protocol: a shell that keeps state
p = subprocess.Popen(["sh"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
p.stdin.write("X=41\necho start\n"); p.stdin.flush()
time.sleep(0.2)
print(repr(rd(p, 1)))
p.stdin.write("X=$((X+1)); echo $X; echo oops >&2\n"); p.stdin.flush()
time.sleep(0.2)
print(repr(rd(p, 1)))

# runaway: no output within timeout, then kill the whole group
p.stdin.write("sleep 30\n"); p.stdin.flush()
t0 = time.time()
print(rd(p, 0.3))
print(p.poll())
p.kill()
print(p.wait() != 0)
print(time.time() - t0 < 5)

# bytes mode and communicate
p = subprocess.Popen(["tr", "a-z", "A-Z"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
print(p.communicate(b"abc"), p.returncode)
p = subprocess.Popen(["sh", "-c", "echo out; echo err >&2; exit 4"], stdin=subprocess.PIPE,
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
print(p.communicate(), p.returncode)
try:
    subprocess.Popen(["nonexistent_cmd_x"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
except FileNotFoundError:
    print("fnf")
