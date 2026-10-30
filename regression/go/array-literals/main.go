// Go1.27 §Composite_literals: for arrays, "An element with a key uses the
// key as its index. ... An element without a key uses the previous
// element's index plus one. If the first element has no key, its index is
// zero." "If fewer elements than the length are provided in the literal,
// the missing elements are set to the zero value". "The notation ...
// specifies an array length equal to the maximum element index plus
// one." Elements are evaluated in order.
package main

import "cprover"

var order int

func next(v int) int {
	order = order*10 + v
	return v
}

func main() {
	filter := [10]int{-1, 4: -2, -3, 9: -4}
	cprover.Assert(filter[0] == -1 && filter[1] == 0 && filter[4] == -2 &&
		filter[5] == -3 && filter[8] == 0 && filter[9] == -4, "keys")

	days := [...]int{3, 4}
	cprover.Assert(len(days) == 2, "[...] length")

	grid := [2][2]int{{1, 2}, {3}}
	cprover.Assert(grid[1][0] == 3 && grid[1][1] == 0, "elided element type")

	e := [3]int{next(1), 2: next(3), 1: next(2)}
	cprover.Assert(order == 132 && e == [3]int{1, 2, 3}, "evaluation order")
}
