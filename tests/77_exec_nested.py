# exec() inside exec()'d code shares the namespace of the outer exec
exec("exec('x = 1')")
print(x)
exec("exec('def helper(n):\\n    return n * 3\\n')\nexec('y = helper(4)')")
print(y, helper(5))
exec("exec('z = 7')\nw = z + 1")
print(z, w)
