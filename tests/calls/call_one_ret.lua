local function a()
    return 3
end
local x = a()
assert(x == 3)
assert(a() == 3)
