// Go1.27 §Function_literals: "A function literal represents an anonymous
// function." "A function literal can be assigned to a variable or invoked
// directly."
package main

import "cprover"

func apply(f func(int) int, x int) int {
	return f(x)
}

func main() {
	f := func(x, y int) int { return x + y }
	cprover.Assert(f(2, 3) == 5, "assigned to a variable and called")

	cprover.Assert(func(x int) int { return x * x }(4) == 16, "invoked directly")

	cprover.Assert(apply(func(x int) int { return -x }, 3) == -3, "as argument")

	// A literal's body has its own labels, results and nested literals.
	g := func(n int) (s int) {
	Loop:
		for i := 0; ; i++ {
			if i == n {
				break Loop
			}
			s += func(v int) int { return v }(i)
		}
		return
	}
	cprover.Assert(g(4) == 6, "nested literal, labels and named result")
}
