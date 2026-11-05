// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: models of a few functions.

// Package runtime models functions of the standard library's runtime
// (go1.27) that affect scheduling only. Every interleaving of goroutines
// is explored anyway, so yielding, the number of processors and garbage
// collection change nothing that a program observes, except the results
// of GOMAXPROCS, which are arbitrary.
package runtime

import "cprover"

// Gosched yields the processor, allowing other goroutines to run.
func Gosched() {}

// GC runs a garbage collection.
func GC() {}

// GOMAXPROCS sets the maximum number of CPUs that can be executing
// simultaneously and returns the previous setting.
func GOMAXPROCS(n int) int {
	prev := cprover.Nondet[int]()
	cprover.Assume(prev >= 1)
	return prev
}
