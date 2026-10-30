// Go1.27 §Slice_types: "A slice is a descriptor for a contiguous segment
// of an underlying array". "The value of an uninitialized slice is nil."
// §Slice_expressions, with the spec's examples: "the slice s has type
// []int, length 3, capacity 4"; "s2[1] == s1[2] == a[5] == 42; they all
// refer to the same underlying array element"; "s3 := s[:0] // s3 == nil";
// "the slice t has type []int, length 2, capacity 4".
// §Making_slices_maps_and_channels: "make([]int, 50, 100)" produces "a new,
// hidden array". §Composite_literals: "the length and capacity of a slice
// literal are the maximum element index plus one."
package main

import "cprover"

func main() {
	a := [5]int{1, 2, 3, 4, 5}
	s := a[1:4]
	cprover.Assert(len(s) == 3 && cap(s) == 4 && s[0] == 2 && s[2] == 4,
		"a[1:4]")
	t := a[1:3:5]
	cprover.Assert(len(t) == 2 && cap(t) == 4 && t[1] == 3, "a[1:3:5]")

	var b [10]int
	s1 := b[3:7]
	s2 := s1[1:4]
	s2[1] = 42
	cprover.Assert(s1[2] == 42 && b[5] == 42 && &s2[1] == &b[5], "shared array")

	var n []int
	cprover.Assert(n == nil && len(n) == 0 && cap(n) == 0, "nil slice")
	cprover.Assert(n[:0] == nil, "slicing nil gives nil")

	m := make([]int, 50, 100)
	cprover.Assert(len(m) == 50 && cap(m) == 100 && m[49] == 0, "make")
	cprover.Assert(m != nil && len(make([]int, 0)) == 0 && make([]int, 0) != nil,
		"make gives non-nil slices")
	m = m[:cap(m)]
	cprover.Assert(len(m) == 100 && m[99] == 0, "reslicing up to the capacity")

	lit := []int{2, 5: 7, 8}
	cprover.Assert(len(lit) == 7 && cap(lit) == 7 && lit[1] == 0 && lit[6] == 8,
		"slice literal")
	grid := [][]int{{1}, {2, 3}}
	cprover.Assert(len(grid[1]) == 2 && grid[1][1] == 3, "slices of slices")
}
