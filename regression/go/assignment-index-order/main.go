// Go1.27 §Assignment_statements: "First, the operands of index expressions
// and pointer indirections (including implicit pointer indirections in
// selectors) on the left and the expressions on the right are all
// evaluated in the usual order. Second, the assignments are carried out in
// left-to-right order." The spec's examples:
//
//	i, x[i] = 1, 2  // set i = 1, x[0] = 2
//	i = 0
//	x[i], i = 2, 1  // set x[0] = 2, i = 1
//	x[1], x[3] = 4, 5  // set x[1] = 4, then panic setting x[3] = 5.
package main

import "cprover"

type T struct{ f int }

func main() {
	var x [3]int
	i := 0
	i, x[i] = 1, 2
	cprover.Assert(i == 1 && x[0] == 2 && x[1] == 0, "i, x[i] = 1, 2")

	x = [3]int{}
	i = 0
	x[i], i = 3, 1
	cprover.Assert(x[0] == 3 && i == 1, "x[i], i = 3, 1")

	p := &T{}
	q := &T{}
	p, p.f = q, 7 // p.f uses the old p
	cprover.Assert(p == q && q.f == 0, "pointer operand evaluated first")

	a := [2]int{1, 2}
	a[0], a[1] = a[1], a[0]
	cprover.Assert(a == [2]int{2, 1}, "swap")

	x[1], x[cprover.Nondet[int]()] = 4, 5
}
