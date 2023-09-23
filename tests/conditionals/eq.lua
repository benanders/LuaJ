local a = 3
local b = 4
local c = 3
assert(a == c)
assert(a ~= b)
assert(a == a)
assert(b == 4)
assert(4 == b)
assert(5 ~= b)
assert(4 == 2 + 2)
assert(5 ~= 2 + 2)
local d = a == 3
assert(d)
assert(d == true)
local e = (a == 3) ~= false
assert(e)
local f = "hi"
assert(f == "hi")
assert(f ~= "hello")
