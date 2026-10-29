// Go1.27 §Order_of_evaluation: "all function calls ... are evaluated in
// lexical left-to-right order". A division whose operands have side
// effects evaluates each operand exactly once, in order.
package main

import "cprover"

var trace int

func first() int {
	trace = trace*10 + 1
	return 100
}

func second() int {
	trace = trace*10 + 2
	return 4
}

func main() {
	s := first() + second()
	cprover.Assert(s == 104 && trace == 12, "left to right")
	trace = 0
	q := first() / second()
	cprover.Assert(q == 25 && trace == 12, "each operand once, in order")
}
