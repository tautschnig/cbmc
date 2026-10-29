// Go1.27 §Assignment_statements: "The assignment proceeds in two phases.
// First, the operands ... on the right are all evaluated in the usual
// order. Second, the assignments are carried out in left-to-right order."
// Also op-assignments "x op= y" and the blank identifier.
package main

import "cprover"

func main() {
	a, b := 1, 2
	a, b = b, a
	cprover.Assert(a == 2 && b == 1, "swap")
	x, y, z := 1, 2, 3
	x, y, z = z, x, y
	cprover.Assert(x == 3 && y == 1 && z == 2, "rotate")
	n := 10
	n -= 3
	n *= 2
	n <<= 1
	n &^= 4
	cprover.Assert(n == 24, "op-assignments")
	_ = n
}
