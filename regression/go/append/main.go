// Go1.27 §Appending_and_copying_slices, with the spec's examples:
//
//	s0 := []int{0, 0}
//	s1 := append(s0, 2)                // append a single element     s1 is []int{0, 0, 2}
//	s2 := append(s1, 3, 5, 7)          // append multiple elements    s2 is []int{0, 0, 2, 3, 5, 7}
//	s3 := append(s2, s0...)            // append a slice              s3 is []int{0, 0, 2, 3, 5, 7, 0, 0}
//	s4 := append(s3[3:6], s3[2:]...)   // append overlapping slice    s4 is []int{3, 5, 7, 2, 3, 5, 7, 0, 0}
//	var b []byte
//	b = append(b, "bar"...)            // append string contents      b is []byte{'b', 'a', 'r' }
//
// "If the capacity of s is not large enough to fit the additional values,
// append allocates a new, sufficiently large underlying array that fits
// both the existing slice elements and the additional values. Otherwise,
// append re-uses the underlying array."
package main

import "cprover"

func equal(a, b []int) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func main() {
	s0 := []int{0, 0}
	s1 := append(s0, 2)
	cprover.Assert(equal(s1, []int{0, 0, 2}), "s1")
	s2 := append(s1, 3, 5, 7)
	cprover.Assert(equal(s2, []int{0, 0, 2, 3, 5, 7}), "s2")
	s3 := append(s2, s0...)
	cprover.Assert(equal(s3, []int{0, 0, 2, 3, 5, 7, 0, 0}), "s3")
	s4 := append(s3[3:6], s3[2:]...)
	cprover.Assert(equal(s4, []int{3, 5, 7, 2, 3, 5, 7, 0, 0}), "s4")
	var b []byte
	b = append(b, "bar"...)
	cprover.Assert(len(b) == 3 && b[0] == 'b' && b[2] == 'r', "bytes of a string")

	a := make([]int, 1, 2)
	r := append(a, 1) // fits: re-uses the array
	r[0] = 9
	cprover.Assert(a[0] == 9, "re-used underlying array")
	g := append(a, 1, 2) // does not fit: a new array
	g[0] = 7
	cprover.Assert(a[0] == 9 && cap(g) >= 3, "new underlying array")
	cprover.Assert(len(append(a)) == 1, "no values")
}
