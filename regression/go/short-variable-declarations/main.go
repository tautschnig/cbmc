// Go1.27 §Short_variable_declarations: a short variable declaration may
// redeclare variables declared earlier in the same block; "Redeclaration
// does not introduce a new variable; it just assigns a new value".
// §Declarations_and_scope: an inner declaration shadows an outer one.
package main

import "cprover"

func main() {
	a, b := 1, 2
	a, c := 3, 4 // a is redeclared (assigned), c is new
	cprover.Assert(a == 3 && b == 2 && c == 4, "redeclaration assigns")
	x := 1
	{
		x := 2 // shadows
		x++
		cprover.Assert(x == 3, "inner x")
	}
	cprover.Assert(x == 1, "outer x unchanged")
	y := 5
	{
		y := y + 1 // the new y's scope begins after the declaration
		cprover.Assert(y == 6, "initialiser refers to the outer y")
	}
	cprover.Assert(y == 5, "outer y unchanged")
}
