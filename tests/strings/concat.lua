local a = "hi"
local b = "hello"
local c = a .. b
assert(c == "hihello")
local d = b .. a
assert(d == "hellohi")
local e = "test"
local f = a .. b .. e
assert(f == "hihellotest")
