// Go1.27 §Conversions (between numeric types): "When converting between
// integer types, if the value is a signed integer, it is sign extended to
// implicit infinite precision; otherwise it is zero extended. It is then
// truncated to fit in the result type's size."
package main

import "cprover"

func main() {
	var u uint8 = 200
	cprover.Assert(int8(u) == -56, "uint8 200 -> int8 -56")
	var m int8 = -1
	cprover.Assert(uint16(m) == 65535, "int8 -1 -> uint16 sign-extends")
	cprover.Assert(int64(m) == -1, "int8 -1 -> int64 -1")
	var big int = 300
	cprover.Assert(uint8(big) == 44, "int 300 -> uint8 truncates")
	cprover.Assert(uint32(u) == 200, "uint8 -> uint32 zero-extends")
}
