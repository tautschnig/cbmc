// Copyright 2017 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: package sort is the real one (go1.27,
// see sourceModels in models.go) except for these functions, in which the
// length and the swapping of the slice use the front-end's intrinsics
// rt.SliceLen and rt.SliceSwap instead of package internal/reflectlite.

package sort

import (
	"cprover/rt"
	"math/bits"
)

// Slice sorts the slice x given the provided less function.
// It panics if x is not a slice.
func Slice(x any, less func(i, j int) bool) {
	length := rt.SliceLen(x)
	swap := func(i, j int) { rt.SliceSwap(x, i, j) }
	limit := bits.Len(uint(length))
	pdqsort_func(lessSwap{less, swap}, 0, length, limit)
}

// SliceStable sorts the slice x using the provided less
// function, keeping equal elements in their original order.
// It panics if x is not a slice.
func SliceStable(x any, less func(i, j int) bool) {
	swap := func(i, j int) { rt.SliceSwap(x, i, j) }
	stable_func(lessSwap{less, swap}, rt.SliceLen(x))
}

// SliceIsSorted reports whether the slice x is sorted according to the
// provided less function. It panics if x is not a slice.
func SliceIsSorted(x any, less func(i, j int) bool) bool {
	n := rt.SliceLen(x)
	for i := n - 1; i > 0; i-- {
		if less(i, i-1) {
			return false
		}
	}
	return true
}
