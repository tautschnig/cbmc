// Go1.27 §Comparison_operators: "Two struct values are equal if their
// corresponding non-blank field values are equal." "Two array values are
// equal if their corresponding element values are equal."
package main

import "cprover"

type S struct {
	a int
	_ int // blank fields are not compared
	b bool
}

func main() {
	var x, y S
	x.a, y.a = 1, 1
	x.b, y.b = true, true
	cprover.Assert(x == y, "equal non-blank fields")
	y.b = false
	cprover.Assert(x != y, "a differing field")

	a := [2]S{x, x}
	b := a
	cprover.Assert(a == b, "arrays of structs with blank fields")
	b[1].a = 2
	cprover.Assert(a != b, "a differing element")

	i := [3]int{cprover.Nondet[int](), 2, 3}
	j := [3]int{i[0], 2, 3}
	cprover.Assert(i == j, "integer arrays")
}
