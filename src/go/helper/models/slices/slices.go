// Copyright 2023 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: a subset of package slices (go1.27).

// Package slices models functions of the standard library's slices.
package slices

import "cmp"

// Equal reports whether two slices are equal: the same length and all
// elements equal. Empty and nil slices are considered equal. Floating
// point NaNs are not considered equal.
func Equal[S ~[]E, E comparable](s1, s2 S) bool {
	if len(s1) != len(s2) {
		return false
	}
	for i := range s1 {
		if s1[i] != s2[i] {
			return false
		}
	}
	return true
}

// Index returns the index of the first occurrence of v in s, or -1 if not
// present.
func Index[S ~[]E, E comparable](s S, v E) int {
	for i := range s {
		if v == s[i] {
			return i
		}
	}
	return -1
}

// Contains reports whether v is present in s.
func Contains[S ~[]E, E comparable](s S, v E) bool {
	return Index(s, v) >= 0
}

// Reverse reverses the elements of the slice in place.
func Reverse[S ~[]E, E any](s S) {
	for i, j := 0, len(s)-1; i < j; i, j = i+1, j-1 {
		s[i], s[j] = s[j], s[i]
	}
}

// Max returns the maximal value in x. It panics if x is empty. For
// floating-point E, Max propagates NaNs (any NaN value in x forces the
// output to be NaN).
func Max[S ~[]E, E cmp.Ordered](x S) E {
	if len(x) < 1 {
		panic("slices.Max: empty list")
	}
	m := x[0]
	for i := 1; i < len(x); i++ {
		v := x[i]
		m = max(m, v)
	}
	return m
}

// Min returns the minimal value in x. It panics if x is empty. For
// floating-point numbers, Min propagates NaNs (any NaN value in x forces
// the output to be NaN).
func Min[S ~[]E, E cmp.Ordered](x S) E {
	if len(x) < 1 {
		panic("slices.Min: empty list")
	}
	m := x[0]
	for i := 1; i < len(x); i++ {
		v := x[i]
		m = min(m, v)
	}
	return m
}
