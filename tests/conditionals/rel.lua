local a = 3
local b = 4
assert(a < b)
assert(a <= b)
assert(b > a)
assert(b >= a)
local c = 3
assert(c >= a)
assert(a <= c)
assert(a < 10)
assert(10 > a)
assert(5 > 4)
assert(2 <= 100)
