// Go1.27 §For_range: "For an array, pointer to array, or slice value a, the
// index iteration values are produced in increasing order, starting at
// element index 0." "For a nil slice, the number of iterations is 0." The
// range expression is evaluated once, but a slice shares its array, so
// changes to elements are seen.
package main

import "cprover"

func main() {
	s := []int{1, 2, 3}
	sum := 0
	for i, v := range s {
		if i == 0 {
			s[2] = 30 // seen: the slice is not copied
		}
		sum += v
	}
	cprover.Assert(sum == 33, "elements are read from the shared array")

	count := 0
	for range s {
		s = append(s, 0) // the length is evaluated once
		count++
	}
	cprover.Assert(count == 3, "the range expression is evaluated once")

	var empty []int
	for range empty {
		cprover.Assert(false, "no iterations over a nil slice")
	}
}
