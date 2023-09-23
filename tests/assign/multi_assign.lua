local a = 3
local b = 4
local c = 5
local d = 6
a, b = 10, 11
assert(a == 10)
assert(b == 11)
a, b, c = 100, 101
assert(a == 100)
assert(b == 101)
assert(c == nil)
a, b, c = 6
assert(a == 6)
assert(b == nil)
assert(c == nil)
a = 4, 5
assert(a == 4)
a, b = 8, 9, 10
assert(a == 8)
assert(b == 9)
