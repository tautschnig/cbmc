// Go1.27 §Min_and_max: arguments with side effects (here, index checks and
// calls) are evaluated once each, in order. "For string arguments the
// result for min is the first argument with the smallest (or for max,
// largest) value, compared lexically byte-wise".
package main

import "cprover"

var calls int

func next() int {
	calls++
	return calls
}

func main() {
	xs := []int{4, cprover.Nondet[int](), 9}
	m := max(xs[0], xs[1], xs[2])
	cprover.Assert(m >= xs[0] && m >= xs[1] && m >= 9, "max of indexed")
	cprover.Assert(min(next(), next(), next()) == 1 && calls == 3, "once each")
	t := max("", "foo", "bar")
	cprover.Assert(t == "foo", "max string")
	s := cprover.Nondet[string]()
	cprover.Assume(len(s) <= 2)
	cprover.Assert(min(s, "b") <= "b", "min string")
	i := cprover.Nondet[int]()
	_ = min(xs[i], 0) // index out of range unless 0 <= i < 3
}
