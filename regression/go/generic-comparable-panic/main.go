// Go1.27 §Satisfying_a_type_constraint: "Because of the exception in the
// constraint satisfaction rule, comparing operands of type parameter type
// may panic at run-time (even though comparable type parameters are always
// strictly comparable)."
package main

import "cprover"

func Eq[T comparable](a, b T) bool { return a == b }

func main() {
	var a, b any = 1, 1
	cprover.Assert(Eq(a, b), "comparable dynamic type")
	if cprover.Nondet[bool]() {
		a, b = []int{}, []int{}
	}
	Eq(a, b)
}
