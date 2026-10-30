// Go1.27 §Array_types: "The length is part of the array's type". Arrays are
// values: assignment and parameter passing copy them. §Index_expressions:
// "For a of pointer to array type: a[x] is shorthand for (*a)[x]".
// §Length_and_capacity: len of an array is its length.
package main

import "cprover"

func double(a [3]int) [3]int {
	for i := 0; i < len(a); i++ {
		a[i] *= 2
	}
	return a
}

func fill(p *[3]int, v int) {
	for i := range len(p) {
		p[i] = v
	}
}

func get() [4]int { return [4]int{} }

func main() {
	var a [3]int
	cprover.Assert(a[0] == 0 && a[2] == 0, "zero value")
	a[1] = 5
	b := a
	b[1] = 6
	cprover.Assert(a[1] == 5, "array assignment copies")
	c := double(a)
	cprover.Assert(a[1] == 5 && c[1] == 10, "arrays are passed by value")

	fill(&a, 7)
	cprover.Assert(a[0] == 7 && a[2] == 7, "indexing through a pointer")

	var grid [2][3]int
	grid[1][2] = 9
	cprover.Assert(grid[1][2] == 9 && grid[0][2] == 0, "arrays of arrays")

	i := cprover.Nondet[int]()
	cprover.Assume(i >= 0 && i < 3)
	a[i] = 1
	cprover.Assert(a[i] == 1, "non-constant index")

	cprover.Assert(len(get()) == 4, "len evaluates a call operand")
}
