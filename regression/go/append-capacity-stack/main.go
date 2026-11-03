// Go1.27 §Appending_and_copying_slices leaves the capacity of the array
// that append allocates open: with --go-compiler any, it is any capacity
// that fits the elements, and with gc's it may also be a stack buffer.
package main

import "cprover"

func main() {
	s := append(make([]int, 4), 1)
	cprover.Assert(cap(s) >= 5, "fits the elements")
	cprover.Assert(cap(s) == 8, "gc doubles")
	var t []int
	t = append(t, 1)
	cprover.Assert(cap(t) == 1, "no stack buffer")
}
