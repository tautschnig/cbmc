// Go1.27 §Conversions_from_slice_to_array_or_array_pointer: "Converting a
// slice to an array yields an array containing the elements of the
// underlying array of the slice. Similarly, converting a slice to an array
// pointer yields a pointer to the underlying array of the slice."
package main

import "cprover"

type ip4 [4]byte

func main() {
	s := make([]byte, 2, 4)
	s[0], s[1] = 7, 8

	a0 := [0]byte(s)
	cprover.Assert(len(a0) == 0, "an empty array")
	a1 := [1]byte(s[1:])
	cprover.Assert(a1[0] == s[1], "a1[0] == s[1]")
	a2 := [2]byte(s)
	cprover.Assert(a2[0] == 7 && a2[1] == 8, "a2[0] == s[0]")
	a2[0] = 1
	cprover.Assert(s[0] == 7, "the array is a copy")

	s1 := (*[1]byte)(s[1:])
	cprover.Assert(&s1[0] == &s[1], "&s1[0] == &s[1]")
	s2 := (*[2]byte)(s)
	s2[1] = 9
	cprover.Assert(s[1] == 9, "the pointer refers to the slice's array")
	s0 := (*[0]byte)(s)
	cprover.Assert(s0 != nil, "s0 != nil")

	var t []string
	t0 := [0]string(t)
	cprover.Assert(len(t0) == 0, "ok for nil slice t")
	t1 := (*[0]string)(t)
	cprover.Assert(t1 == nil, "t1 == nil")

	u := make([]byte, 0)
	u0 := (*[0]byte)(u)
	cprover.Assert(u0 != nil, "u0 != nil")

	addr := ip4([]byte{10, 0, 0, 1})
	cprover.Assert(addr[0] == 10 && addr[3] == 1, "to a named array type")
	n := cprover.Nondet[int]()
	cprover.Assume(n >= 0 && n <= 4)
	v := make([]int, n)
	if n >= 3 {
		w := [3]int(v)
		cprover.Assert(w[2] == 0, "a slice of a nondeterministic length")
	}
}
