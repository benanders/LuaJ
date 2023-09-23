local a = 3
local b = 4
if a == 3 then
    assert(true)
elseif b == 4 then
    assert(false)
end
if a ~= 3 then
    assert(false)
elseif b ~= 4 then
    assert(false)
end
if a ~= 3 then
    assert(false)
elseif b == 4 then
    assert(true)
end
if a ~= 3 then
    assert(false)
elseif b == 4 then
    assert(true)
else
    assert(false)
end
if a ~= 3 then
    assert(false)
elseif b ~= 4 then
    assert(false)
else
    assert(true)
end
