local a = 3
local b = 4
local c = a + b
assert(c == 7)
local d = a + 10
assert(d == 13)
local e = 11 + b
assert(e == 15)
local f = a + b + c
assert(f == 14)
local g = a + b + 10 + c
assert(g == 24)
local h = 24 + 15
assert(h == 39)
