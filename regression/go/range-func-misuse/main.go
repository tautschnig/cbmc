// Go1.27 §For_range: "If the loop body terminates (such as by a break
// statement), yield returns false and must not be called again." As in gc,
// a call of yield after the body ended the loop, after the loop, or after a
// panic of the body that the iterator recovered, is a run-time error.
package main

import "cprover"

func ignoring(yield func(int) bool) {
	yield(1)
	yield(2) // after the body returned false
}

var saved func(int) bool

func saving(yield func(int) bool) {
	saved = yield
	yield(1)
}

func recovering(yield func(int) bool) {
	defer func() { recover() }()
	yield(1)
}

func main() {
	switch cprover.Nondet[int]() {
	case 0:
		for range ignoring {
			break
		}
	case 1:
		for range saving {
		}
		saved(2) // after the loop
	case 2:
		for range recovering {
			var p *int
			*p = 1
		}
	}
}
