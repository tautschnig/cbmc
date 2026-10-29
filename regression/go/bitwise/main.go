// Go1.27 §Arithmetic_operators: & | ^ &^ (bit clear, AND NOT); unary ^x is
// the bitwise complement, "m ^ x with m = "all bits set to 1" for unsigned
// x and m = -1 for signed x"; unary -x is 0 - x.
package main

import "cprover"

func main() {
	a, b := 0b1100, 0b1010
	cprover.Assert(a&b == 0b1000, "and")
	cprover.Assert(a|b == 0b1110, "or")
	cprover.Assert(a^b == 0b0110, "xor")
	cprover.Assert(a&^b == 0b0100, "and not")
	var u uint8 = 0x0f
	cprover.Assert(^u == 0xf0, "complement of unsigned")
	var i int8 = 5
	cprover.Assert(^i == -6, "complement of signed is -x-1")
	cprover.Assert(-u == 0xf1, "negation of unsigned wraps")
}
