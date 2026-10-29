// Go1.27 §Function_types: "The value of an uninitialized variable of
// function type is nil." §Comparison_operators: function values may only
// be compared to nil. A declared function can be used as a value.
package main

import "cprover"

func double(x int) int { return 2 * x }
func negate(x int) int { return -x }

var global func(int) int // nil until main assigns it

func pick(neg bool) func(int) int {
	if neg {
		return negate
	}
	return double
}

func main() {
	cprover.Assert(global == nil, "package-level function variable is nil")
	var local func(int) int
	cprover.Assert(nil == local, "local function variable is nil")

	global = double
	cprover.Assert(global != nil, "a declared function is not nil")
	cprover.Assert(global(21) == 42, "call through a variable")

	neg := cprover.Nondet[bool]()
	h := pick(neg)
	r := h(5)
	cprover.Assert(neg == (r == -5), "function value returned from a call")
	cprover.Assert(pick(true)(3) == -3, "call of a call's result")

	global = nil
	cprover.Assert(global == nil, "assigning nil")
}
