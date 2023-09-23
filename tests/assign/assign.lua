local a = 3
local b = 4
assert(a == 3)
a = 4
assert(a == 4)
a = 10
assert(a == 10)
a = b
assert(a == 4)
a = (b + 24) * 2
assert(a == 56)
a = "hi"
assert(a == "hi")
