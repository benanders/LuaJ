local function a(a, b)
    return a + 1, b + 2, a + 3
end
local x = a(1, 2)
assert(x == 2)
local y, z = a(1, 2)
assert(y == 2)
assert(z == 4)
local j, k, l, m = a(1, 2)
assert(j == 2)
assert(k == 4)
assert(l == 4)
assert(m == nil)

x, y = a(1, 2), 100
assert(x == 2)
assert(y == 100)
x, y, z = 50, a(1, 2)
assert(x == 50)
assert(y == 2)
assert(z == 4)
x, y, z = 50, a(1, 2), 40
assert(x == 50)
assert(y == 2)
assert(z == 40)
