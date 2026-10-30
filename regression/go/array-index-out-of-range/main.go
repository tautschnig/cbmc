// Go1.27 §Index_expressions: "the index x is in range if 0 <= x < len(a),
// otherwise it is out of range"; for arrays, "if x is out of range at run
// time, a run-time panic occurs". This applies to reads, writes and &a[x]
// (§Address_operators: "If the evaluation of x would cause a run-time
// panic, then the evaluation of &x does too.").
package main

import "cprover"

func main() {
	var a [4]int
	i := cprover.Nondet[int]()
	switch cprover.Nondet[int]() {
	case 0:
		_ = a[i]
	case 1:
		a[i] = 1
	case 2:
		_ = &a[i]
	case 3:
		var u uint8 = cprover.Nondet[uint8]()
		cprover.Assume(u < 4)
		_ = a[u] // always in range
	}
}
