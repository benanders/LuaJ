local a = function() return 3 end
local function b(x)
    assert(x() == 3)
end
b(a)
