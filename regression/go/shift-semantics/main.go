// Go1.27 §Arithmetic_operators: "The shift operators shift the left operand
// by the shift count ... There is no upper limit on the shift count. Shifts
// behave as if the left operand is shifted n times by 1". Arithmetic shifts
// for signed, logical shifts for unsigned operands.
package main

import "cprover"

func main() {
	var one uint64 = 1
	var s, t uint = 64, 70
	cprover.Assert(one<<s == 0, "1 << 64 == 0")
	cprover.Assert(one<<t == 0, "1 << 70 == 0")
	var neg int64 = -8
	cprover.Assert(neg>>s == -1, "-8 >> 64 == -1")
	cprover.Assert(neg>>t == -1, "-8 >> 70 == -1")
	var pos int64 = 8
	cprover.Assert(pos>>t == 0, "8 >> 70 == 0")
	var u8 uint8 = 0x81
	var one8 uint = 1
	cprover.Assert(u8<<one8 == 2, "uint8 shift is modulo 2^8")
	cprover.Assert(u8>>one8 == 0x40, "logical right shift for unsigned")
	var i8 int8 = -127
	cprover.Assert(i8>>one8 == -64, "arithmetic right shift for signed")
}
