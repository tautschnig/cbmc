// Go1.27 §Return_statements: results may be listed, be "a single call to a
// multi-valued function", or be omitted when the result parameters are
// named, which are "initialized to the zero values for their type upon
// entry to the function". §Assignment_statements: "x, y = f() assigns the
// first value to x and the second to y." §Calls: f(g()) passes the
// results of g to the parameters of f in order.
package main

import "cprover"

func divmod(a, b int) (int, int) {
	return a / b, a % b
}

func forward(a, b int) (int, int) {
	return divmod(a, b)
}

func named(flag bool) (n int, ok bool) {
	if flag {
		n = 7
		ok = true
	}
	return
}

func sum(q, r int) int {
	return q + r
}

var pq, pr = divmod(17, 5) // package-level, initialised from one call

func main() {
	q, r := divmod(17, 5)
	cprover.Assert(q == 3 && r == 2, "x, y := f()")

	q, r = forward(-17, 5)
	cprover.Assert(q == -3 && r == -2, "return f()")

	n, ok := named(false)
	cprover.Assert(n == 0 && !ok, "named results start as zero values")
	n, ok = named(true)
	cprover.Assert(n == 7 && ok, "bare return returns the named results")

	_, r = divmod(9, 4)
	cprover.Assert(r == 1, "blank identifier discards a result")

	cprover.Assert(sum(divmod(17, 5)) == 5, "f(g())")

	var a, b = divmod(8, 3)
	cprover.Assert(a == 2 && b == 2, "var a, b = f()")
	cprover.Assert(pq == 3 && pr == 2, "package-level var a, b = f()")

	// Both phases of the assignment: the call is evaluated before q and r
	// are assigned, so its arguments see the old values.
	q, r = 20, 6
	q, r = divmod(q, r)
	cprover.Assert(q == 3 && r == 2, "operands evaluated before assigning")
}
