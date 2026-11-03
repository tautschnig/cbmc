// Packages slices, sort (its functions on slices of built-in types), cmp
// and math/bits are modelled by their own source (see sourceModels in
// src/go/helper/models.go; slices.overlaps is replaced).
package main

import (
	"cmp"
	"cprover"
	"math/bits"
	"slices"
	"sort"
)

func main() {
	a := []int{cprover.Nondet[int](), cprover.Nondet[int](), cprover.Nondet[int]()}
	b := slices.Clone(a)
	slices.Sort(b)
	cprover.Assert(slices.IsSorted(b) && slices.Contains(b, a[1]), "slices.Sort")
	_, found := slices.BinarySearch(b, a[2])
	cprover.Assert(found, "slices.BinarySearch")
	cprover.Assert(slices.Max(a) == b[2] && slices.Min(a) == b[0], "max/min")
	sort.Ints(a)
	cprover.Assert(sort.IntsAreSorted(a) && slices.Equal(a, b), "sort.Ints")
	// Insert may shift b's elements in place (if Clone left room).
	c := slices.Insert(b, 1, 7)
	cprover.Assert(len(c) == 4 && c[1] == 7 && c[2] == a[1], "slices.Insert")
	cprover.Assert(cmp.Compare(1, 2) == -1 && cmp.Less("a", "b"), "cmp")

	x := cprover.Nondet[uint64]()
	hi, lo := bits.Mul64(x, 2)
	cprover.Assert(hi == x>>63 && lo == x<<1, "bits.Mul64")
	cprover.Assert(bits.Len(8) == 4 && bits.OnesCount8(0xf0) == 4, "bits")
}
