local a = 3
local b = 4
local c, d = a, b
assert(c == 3)
assert(d == 4)
local e, f, g = 6, 7, 8
assert(e == 6)
assert(f == 7)
assert(g == 8)

local h, i = 10, 11, 12, 13
assert(h == 10)
assert(i == 11)

local j, k, l, m = 8, 9
assert(j == 8)
assert(k == 9)
assert(l == nil)
assert(m == nil)
