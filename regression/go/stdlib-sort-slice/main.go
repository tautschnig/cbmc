// The model of sort.Slice, SliceStable and SliceIsSorted: the source of
// package sort, with the length and swapping of the slice by the
// front-end's intrinsics (reflectlite there).
package main

import (
	"cprover"
	"sort"
)

type pair struct {
	k string
	v int
}

func main() {
	ps := []pair{{"b", 2}, {"a", 1}, {"c", 3}}
	sort.Slice(ps, func(i, j int) bool { return ps[i].k < ps[j].k })
	cprover.Assert(ps[0].k == "a" && ps[1].k == "b" && ps[2].v == 3,
		"sort.Slice")
	cprover.Assert(sort.SliceIsSorted(ps, func(i, j int) bool {
		return ps[i].v < ps[j].v
	}), "sorted")
	xs := []int{3, 1, 2}
	sort.SliceStable(xs, func(i, j int) bool { return xs[i] < xs[j] })
	cprover.Assert(xs[0] == 1 && xs[2] == 3, "SliceStable")
}
