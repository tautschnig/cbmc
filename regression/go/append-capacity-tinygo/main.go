// Go1.27 §Appending_and_copying_slices leaves the capacity of the array
// that append allocates open. TinyGo 0.44 (--go-compiler tinygo) gives
// the smallest power of two above the new length, also to slices that do
// not escape (src/go/helper/runtime/rt_tinygo_test.go checks this against
// TinyGo).
package main

import "cprover"

func main() {
	var t []int
	t = append(t, 1)
	cprover.Assert(cap(t) == 2, "first append")
	s := append(make([]int, 4), 1)
	cprover.Assert(cap(s) == 8, "doubled")
	s = append(make([]int, 512), 1)
	cprover.Assert(cap(s) == 1024, "doubled beyond 256")
	s = append(make([]int, 3), 1, 2, 3, 4, 5, 6, 7)
	cprover.Assert(cap(s) == 16, "above the new length")
	b := append(make([]byte, 33), 'x')
	cprover.Assert(cap(b) == 64, "whatever the element size")
	n := cprover.Nondet[int]()
	cprover.Assume(0 <= n && n <= 300)
	v := append(make([]int, n), 1)
	cprover.Assert(cap(v) > n+1 && cap(v) <= 2*n+2, "symbolic length")
}
