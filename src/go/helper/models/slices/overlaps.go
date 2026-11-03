// Copyright 2021 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: package slices is the real one (go1.27,
// see sourceModels in models.go) except for overlaps, which compares
// addresses converted to uintptr through package unsafe.

package slices

// overlaps reports whether the memory ranges a[0:len(a)] and b[0:len(b)]
// overlap. The ranges are contiguous, so that they overlap if and only if
// the first element of one is an element of the other; unlike the real
// overlaps, which tests sizes, slices of zero-size elements may overlap,
// for which Insert and Replace then copy more than needed.
func overlaps[E any](a, b []E) bool {
	if len(a) == 0 || len(b) == 0 {
		return false
	}
	for i := range b {
		if &a[0] == &b[i] {
			return true
		}
	}
	for i := range a {
		if &b[0] == &a[i] {
			return true
		}
	}
	return false
}
