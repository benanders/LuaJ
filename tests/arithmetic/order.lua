local a = 3
local b = 4
local c = a + b * 2
assert(c == 11)
local d = a * 2 + b
assert(d == 10)
local e = a * 2 + b * 2
assert(e == 14)
local f = (a + b) * 2
assert(f == 14)
local g = 2 * (a + b)
assert(g == 14)
local h = a + b^2 * 2 - b
assert(h == 31)
local i = (a + b)^2
assert(i == 49)
local j = -3 + a
assert(j == 0)
