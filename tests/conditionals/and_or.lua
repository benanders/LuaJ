local a = 3
local b = 4
local x = 100
local c = a == 3 and b == 4
assert(c == true)
local d = a == 3 and b == 5
assert(d == false)
local e = a == 4 and b == 4
assert(e == false)
local f = a == 3 and b == 4 and x == 100
assert(f == true)

local g = a == 3 or b == 4
assert(g == true)
local h = a == 3 or b == 4
assert(h == true)
local i = a == 4 or b == 4
assert(i == true)
local j = a == 5 or b == 6
assert(j == false)
local k = a == 5 or b == 6 or x == 100
assert(k == true)
local l = a == 5 or b == 4 or x == 12
assert(l == true)

local m = (a == 3 and b == 5) or x == 100
assert(m == true)
local n = (a == 3 or b == 5) and x == 100
assert(n == true)

local o = not (a == 3 and b == 4)
assert(o == false)
local p = not (a == 4) and b == 4
assert(p == true)
local q = not (a == 5 or b == 4 or x == 12)
assert(q == false)
