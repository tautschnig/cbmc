// Go1.27 §Type_switches: "The TypeSwitchGuard may include a short variable
// declaration. When that form is used, the variable is declared at the end
// of the TypeSwitchCase in the implicit block of each clause." Such a
// variable belongs to the function literal (or the body of a range over a
// function) that contains the switch; it is not captured from outside.
package main

import "cprover"

func resolve(in any) (out int) {
	defer func() {
		switch v := in.(type) {
		case int:
			out = v + 1
		case string:
			out = len(v)
		}
	}()
	return 0
}

func seq(n int) func(func(int) bool) {
	return func(yield func(int) bool) {
		for i := 0; i < n; i++ {
			if !yield(i) {
				return
			}
		}
	}
}

func main() {
	cprover.Assert(resolve(3) == 4, "the int clause binds v to the int")
	cprover.Assert(resolve("ab") == 2, "the string clause binds v to it")

	values := []any{1, "xyz", 2.5}
	total := 0
	for i := range seq(len(values)) {
		switch v := values[i].(type) {
		case int:
			total += v
		case string:
			total += len(v)
		default:
			total += 100
		}
	}
	cprover.Assert(total == 104, "and in the body of a range over a function")
}
