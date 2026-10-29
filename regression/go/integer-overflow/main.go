// Go1.27 §Integer_overflow: "For unsigned integer values, the operations
// +, -, *, and << are computed modulo 2n, where n is the bit width of the
// unsigned integer's type." (The spec typesets 2n as 2 to the power n.)
// "For signed integers, the operations +, -, *, /, and << may legally
// overflow and the resulting value exists and is deterministically defined
// by the signed integer representation, the operation, and its operands.
// Overflow does not cause a run-time panic." It must not be reported.
package main

import "cprover"

func main() {
	var i8 int8 = 127
	i8++
	cprover.Assert(i8 == -128, "int8 127+1 wraps to -128")
	var u8 uint8 = 0
	u8--
	cprover.Assert(u8 == 255, "uint8 0-1 wraps to 255")
	var i64 int64 = -9223372036854775808
	i64 = i64 - 1
	cprover.Assert(i64 == 9223372036854775807, "int64 min-1 wraps to max")
	var u uint = 0
	u = -u - 1
	cprover.Assert(u == 18446744073709551615, "uint 0-1 wraps (64-bit)")
	x := cprover.Nondet[int32]()
	y := x * 2
	cprover.Assert(y == x+x, "multiplication wraps consistently")
}
