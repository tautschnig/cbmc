// Go1.27 §Defer_statements: "Each time a "defer" statement executes, the
// function value and parameters to the call are evaluated as usual and
// saved anew but the actual function is not invoked. Instead, deferred
// functions are invoked immediately before the surrounding function
// returns, in the reverse order they were deferred. That is, if the
// surrounding function returns through an explicit return statement,
// deferred functions are executed after any result parameters are set by
// that return statement but before the function returns to its caller."
package main

import "cprover"

var trace []int

func order() {
	defer func() { trace = append(trace, 1) }()
	defer func() { trace = append(trace, 2) }()
	trace = append(trace, 0)
}

// Deferred in a loop: one call per execution of the defer statement.
func loop(n int) {
	for i := 0; i < n; i++ {
		defer func(x int) { trace = append(trace, x) }(i * 10)
	}
}

// Deferred where goto repeats the defer statement.
func repeat(n int) {
	i := 0
again:
	if i < n {
		defer func(x int) { trace = append(trace, x) }(i)
		i++
		goto again
	}
}

// With the spec's example: "f returns 42".
func f() (result int) {
	defer func() {
		// result is accessed after it was set to 6 by the return statement
		result *= 7
	}()
	return 6
}

func unnamed() int {
	x := 1
	defer func() { x = 5 }()
	return x // the result is set before the deferred call
}

func swap() (a, b int) {
	a, b = 1, 2
	defer func() { a *= 10 }()
	return b, a
}

func discarded() (n int) {
	defer func() int { n++; return 99 }()
	return 1
}

func main() {
	order()
	cprover.Assert(len(trace) == 3 && trace[0] == 0 && trace[1] == 2 &&
		trace[2] == 1, "reverse order")
	trace = nil
	loop(3)
	cprover.Assert(len(trace) == 3 && trace[0] == 20 && trace[1] == 10 &&
		trace[2] == 0, "parameters evaluated at the defer statement")
	trace = nil
	repeat(2)
	cprover.Assert(len(trace) == 2 && trace[0] == 1 && trace[1] == 0, "goto")
	cprover.Assert(f() == 42, "f returns 42")
	cprover.Assert(unnamed() == 1, "unnamed result")
	a, b := swap()
	cprover.Assert(a == 20 && b == 1, "results set, then modified")
	cprover.Assert(discarded() == 2, "results of deferred calls discarded")
}
