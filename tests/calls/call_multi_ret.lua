local function a(a, b)
    return a + 1, b + 1
end
local x, y = a(1, 2)
assert(x == 2)
assert(y == 3)
