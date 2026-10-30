// Go1.27 §Type_switches, following the spec's example (case nil, single-type
// cases, a multi-type case and default). "In clauses with a case listing exactly one
// type, the variable has that type; otherwise, the variable has the type of
// the expression in the TypeSwitchGuard."
package main

import "cprover"

type Stringer interface{ String() string }
type Name string

func (n Name) String() string { return string(n) }

func classify(x any) int {
	switch i := x.(type) {
	case nil:
		return 0
	case int:
		return i + 100 // i has type int
	case float64:
		return 2
	case func(int) float64:
		return 3
	case bool, string:
		_ = i == x // i has the type of x
		return 4
	case Stringer:
		if i.String() == "go" {
			return 5
		}
		return 6
	default:
		return 7
	}
}

func main() {
	cprover.Assert(classify(nil) == 0, "nil")
	cprover.Assert(classify(1) == 101, "int")
	cprover.Assert(classify(1.5) == 2, "float64")
	cprover.Assert(classify(func(int) float64 { return 0 }) == 3, "function type")
	cprover.Assert(classify(true) == 4 && classify("s") == 4, "bool, string")
	cprover.Assert(classify(Name("go")) == 5, "an interface case")
	cprover.Assert(classify([]int{}) == 7, "default")
	n := 0
	switch any(n).(type) {
	case int:
		n = 1
		break
	}
	cprover.Assert(n == 1, "without a variable; break")
}
