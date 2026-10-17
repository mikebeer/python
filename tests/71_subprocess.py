import subprocess
r = subprocess.run(["sh","-c","cat; echo err >&2; exit 3"], input="hello", capture_output=True, text=True)
print(r.returncode, repr(r.stdout), repr(r.stderr))
try:
    subprocess.run(["sleep","5"], timeout=0.3)
except subprocess.TimeoutExpired as e:
    print("timeout", e)
try:
    subprocess.run(["nonexistent_cmd_x"])
except FileNotFoundError as e:
    print("fnf")
print(subprocess.check_output("echo hi | tr a-z A-Z", shell=True, text=True))
p = subprocess.Popen(["wc","-c"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
print(p.communicate("abcd")[0].strip(), p.returncode)
print(subprocess.call(["sh", "-c", "exit 2"]))
