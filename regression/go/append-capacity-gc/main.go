// Go1.27 §Appending_and_copying_slices: "If the capacity of s is not large
// enough to fit the additional values, append allocates a new,
// sufficiently large underlying array". The spec leaves the capacity
// open; with --go-compiler gc, CBMC gives the capacities gc go1.27 may
// choose (src/go/helper/runtime/rt_test.go checks them against gc).
package main

import "cprover"

type pair struct {
	p *int
	a [100]byte
}

func main() {
	// Beyond gc's 32-byte stack buffers, the capacity is runtime.growslice's:
	// double below 256 elements, then grow by about 1.25, rounded up to a
	// malloc size class.
	s := make([]int, 4)
	s = append(s, 1)
	cprover.Assert(cap(s) == 8, "doubled")
	s = append(make([]int, 256), 1)
	cprover.Assert(cap(s) == 512, "doubled up to 256")
	s = append(make([]int, 512), 1)
	cprover.Assert(cap(s) == 848, "grown by about 1.25 beyond")
	s = append(make([]int, 3), 1, 2, 3, 4, 5, 6, 7)
	cprover.Assert(cap(s) == 10, "the new length if more than double")
	b := append(make([]byte, 33), 'x')
	cprover.Assert(cap(b) == 80, "rounded up to a size class")
	p := append(make([]pair, 5), pair{})
	cprover.Assert(cap(p) == 10, "pointers, below the malloc header size")
	q := append(make([]pair, 5, 5), make([]pair, 5)...)
	cprover.Assert(cap(q) == 10, "appending a slice")

	// Within a stack buffer of 32 bytes, gc may also choose the whole
	// buffer (4 ints) or the length rounded up to a size class, depending
	// on its escape analysis.
	var t []int
	t = append(t, 1)
	cprover.Assert(cap(t) == 1 || cap(t) == 4, "first append")
	u := append(make([]int, 2), 1)
	cprover.Assert(cap(u) == 3 || cap(u) == 4, "into the buffer")
	n := cprover.Nondet[int]()
	cprover.Assume(0 <= n && n <= 300)
	v := append(make([]int, n), 1)
	cprover.Assert(cap(v) > n && cap(v) < 3*n+5, "symbolic length")
}
