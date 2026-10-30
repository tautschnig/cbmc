// Go1.27 §Comparison_operators: "Two interface values are equal if they have
// identical dynamic types and equal dynamic values or if both have value
// nil. A value x of non-interface type X and a value t of interface type T
// can be compared if type X is comparable and X implements T. They are equal
// if t's dynamic type is identical to X and t's dynamic value is equal to x."
// "A comparison of two interface values with identical dynamic types causes
// a run-time panic if that type is not comparable."
package main

import "cprover"

type MyInt int
type P struct{ x, y int }

func main() {
	var a, b any = 1, 1
	cprover.Assert(a == b, "equal dynamic types and values")
	b = MyInt(1)
	cprover.Assert(a != b, "different dynamic types")
	cprover.Assert(a == 1 && b == MyInt(1) && b != 1, "with non-interface values")
	var n1, n2 any
	cprover.Assert(n1 == n2 && n1 != a, "nil")
	cprover.Assert(any(P{1, 2}) == any(P{1, 2}) && any("ab") == any("a"+"b"),
		"structs and strings")
	m := map[any]int{1: 10, "1": 20, P{1, 1}: 30}
	cprover.Assert(m[1] == 10 && m["1"] == 20 && m[P{1, 1}] == 30 && m[MyInt(1)] == 0,
		"interface map keys")
	var s1, s2 any = []int{1}, []int{1}
	_ = s1 == s2 // panics: slices are not comparable
}
