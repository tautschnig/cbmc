// Go1.27 §Arithmetic_operators (Integer operators): q = x / y and r = x % y
// satisfy x = q*y + r and |r| < |y|, with x / y truncated towards zero.
// "if the dividend x is the most negative value for the int type of x, the
// quotient q = x / -1 is equal to x (and r = 0) due to two's-complement
// integer overflow". Operands are variables so that nothing is folded.
package main

import "cprover"

func main() {
	x, y := -7, 2
	cprover.Assert(x/y == -3, "-7 / 2 == -3")
	cprover.Assert(x%y == -1, "-7 % 2 == -1")
	x, y = 7, -2
	cprover.Assert(x/y == -3, "7 / -2 == -3")
	cprover.Assert(x%y == 1, "7 % -2 == 1")
	var m int64 = -9223372036854775808
	var n int64 = -1
	cprover.Assert(m/n == m, "most negative / -1 == most negative")
	cprover.Assert(m%n == 0, "most negative % -1 == 0")
	var b int8 = -128
	var c int8 = -1
	cprover.Assert(b/c == -128, "int8: -128 / -1 == -128")
}
