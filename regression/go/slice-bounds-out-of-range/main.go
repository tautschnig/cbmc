// Go1.27 §Index_expressions: for slices, "if x is out of range at run time,
// a run-time panic occurs". §Slice_expressions: "For slices, the upper
// index bound is the slice capacity cap(a) rather than the length." Full
// slice expressions are in range "if 0 <= low <= high <= max <= cap(a)".
// §Making_slices_maps_and_channels: "if n is negative or larger than m at
// run time, a run-time panic occurs."
package main

import "cprover"

func main() {
	s := make([]int, 2, 4)
	i := cprover.Nondet[int]()
	switch cprover.Nondet[int]() {
	case 0:
		_ = s[i]
	case 1:
		_ = s[:i] // up to cap(s) is in range
	case 2:
		_ = s[0:1:i]
	case 3:
		_ = make([]int, i)
	case 4:
		_ = s[:4] // in range: cap(s) is 4
	}
}
