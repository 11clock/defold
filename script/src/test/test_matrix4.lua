-- constructor
local m = vmath.matrix4()

-- index
assert(m.m00 == 1, "m.m00 is not 1")
assert(m.m10 == 0, "m.m10 is not 0")
assert(m.m20 == 0, "m.m20 is not 0")
assert(m.m30 == 0, "m.m30 is not 0")
assert(m.m01 == 0, "m.m01 is not 0")
assert(m.m11 == 1, "m.m11 is not 1")
assert(m.m21 == 0, "m.m21 is not 0")
assert(m.m31 == 0, "m.m31 is not 0")
assert(m.m02 == 0, "m.m02 is not 0")
assert(m.m12 == 0, "m.m12 is not 0")
assert(m.m22 == 1, "m.m22 is not 1")
assert(m.m32 == 0, "m.m32 is not 0")
assert(m.m03 == 0, "m.m03 is not 0")
assert(m.m13 == 0, "m.m13 is not 0")
assert(m.m23 == 0, "m.m23 is not 0")
assert(m.m33 == 1, "m.m33 is not 1")

-- copy constructor
m.m00 = 2
local m1 = vmath.matrix4(m)
-- equals
assert(m == m1, "m != m1")
-- concat
print("m: " .. m)

-- new index
m.m10 = 5
m.m11 = 6
m.m12 = 7
m.m13 = 8
assert(m.m10 == 5, "q.m10 is not 5")
assert(m.m11 == 6, "q.m11 is not 6")
assert(m.m12 == 7, "q.m12 is not 7")
assert(m.m13 == 8, "q.m13 is not 8")

m.c0 = vmath.vector4(0, 0, 0, 0)
m.c3 = vmath.vector4(1,2,3,4)
assert(m.c0.x == 0)
assert(m.c3.x == 1)
assert(m.c3 == vmath.vector4(1, 2, 3, 4))
local tmp = m.c3
tmp.x = 100
-- ensure copy semantics
assert(m.c3 == vmath.vector4(1, 2, 3, 4))

-- mul by mat
m = vmath.matrix4()
m1 = vmath.matrix4(m)
m.m00 = 1
m.m11 = 2
m.m22 = 3
m.m33 = 4
m1.m00 = 5
m1.m11 = 6
m1.m22 = 7
m1.m33 = 8
m2 = m * m1
assert(m2.m00 == 5, "mul by mat")
assert(m2.m11 == 12, "mul by mat")
assert(m2.m22 == 21, "mul by mat")
assert(m2.m33 == 32, "mul by mat")
-- mul by vec
m = vmath.matrix4()
v = vmath.vector4(1, 2, 3, 4)
v2 = m * v
assert(v == v2, "mul by vec")
-- mul by num
m = vmath.matrix4()
m.m00 = 1
m.m11 = 2
m.m22 = 3
m.m33 = 4
m2 = m * 2
assert(m2.m00 == 2, "mul by num")
assert(m2.m11 == 4, "mul by num")
assert(m2.m22 == 6, "mul by num")
assert(m2.m33 == 8, "mul by num")
