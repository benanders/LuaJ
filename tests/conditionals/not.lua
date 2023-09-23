local a = 3
assert(a)
local b = not a
assert(b == false)
assert(not b)
local c = not false
assert(c == true)
local d = not 3
assert(d == false)

local e = not (a == 3)
assert(e == false)
local f = not (a ~= 3)
assert(f == true)
