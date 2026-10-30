// Go1.27 §Map_types: "The comparison operators == and != must be fully
// defined for operands of the key type". Keys are equal as by ==: strings
// byte-wise, structs field by field (§Comparison_operators).
package main

import "cprover"

type Point struct{ x, y int }

func main() {
	byName := map[string]int{}
	k := "ab"
	byName[k+"c"] = 1 // a computed string equal to "abc"
	cprover.Assert(byName["abc"] == 1 && len(byName) == 1, "string keys")

	grid := map[Point]string{{1, 2}: "a"}
	grid[Point{1, 2}] = "b"
	cprover.Assert(len(grid) == 1 && grid[Point{x: 1, y: 2}] == "b", "struct keys")

	sym := map[int]int{}
	i, j := cprover.Nondet[int](), cprover.Nondet[int]()
	sym[i] = 1
	sym[j] = 2
	cprover.Assert((i == j) == (len(sym) == 1), "nondeterministic keys")
	cprover.Assert(sym[j] == 2, "the last write wins")
}
