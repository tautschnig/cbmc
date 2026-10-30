// Go1.27 §Function_literals: "Function literals are closures: they may
// refer to variables declared in a surrounding function. Those variables
// are then shared between the surrounding function and the function
// literal, and they survive as long as they are accessible."
package main

import "cprover"

func counter(start int) func() int {
	n := start // survives after counter returns
	return func() int {
		n++
		return n
	}
}

func main() {
	x := 1
	inc := func() { x++ }
	inc()
	inc()
	cprover.Assert(x == 3, "the literal shares x with main")
	x = 10
	get := func() int { return x }
	cprover.Assert(get() == 10, "and sees main's assignments")

	c1, c2 := counter(0), counter(100)
	c1()
	cprover.Assert(c1() == 2, "each call of counter has its own n")
	cprover.Assert(c2() == 101, "independent of other closures")

	// Nested literals capture through the enclosing literal.
	total := 0
	add := func(v int) func() {
		return func() { total += v }
	}
	add(3)()
	add(4)()
	cprover.Assert(total == 7, "nested closures share total")

	// Go1.27 §For_clause: "Each iteration has its own separate declared
	// variable". The spec's example: "prints 1 3 5".
	var prints [3]func() int
	k := 0
	for i := 0; i < 5; i++ {
		prints[k] = func() int { return i }
		k++
		i++
	}
	cprover.Assert(prints[0]() == 1 && prints[1]() == 3 && prints[2]() == 5,
		"per-iteration loop variables")
}
