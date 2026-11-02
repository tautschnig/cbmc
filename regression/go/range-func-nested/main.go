// Go1.27 §For_range over functions: nested loops (iter.Seq), a return from
// the inner body with named results and a deferred call, and continue with
// the label of an outer loop.
package main

import (
	"cprover"
	"iter"
)

func count(n int) iter.Seq[int] {
	return func(yield func(int) bool) {
		for i := 0; i < n; i++ {
			if !yield(i) {
				return
			}
		}
	}
}

func firstPair(limit int) (a, b int) {
	defer func() { a++ }() // runs after the loop returns
	for i := range count(limit) {
		for j := range count(limit) {
			if i+j == 3 {
				a, b = i, j
				return
			}
		}
	}
	return -10, -10
}

func search() int {
outer:
	for i := range count(4) {
		for j := range count(4) {
			if j == 1 {
				continue outer
			}
			if i == 3 {
				return i*100 + j
			}
		}
	}
	return -1
}

func main() {
	a, b := firstPair(4)
	cprover.Assert(a == 1 && b == 3, "nested return with defer")
	a, b = firstPair(1)
	cprover.Assert(a == -9 && b == -10, "no pair")
	cprover.Assert(search() == 300, "continue outer and return")
}
