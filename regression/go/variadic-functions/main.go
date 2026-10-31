// Go1.27 §Passing_arguments_to_..._parameters: "If f is variadic with a
// final parameter p of type ...T, then within f the type of p is equivalent
// to type []T. If f is invoked with no actual arguments for p, the value
// passed to p is nil. Otherwise, the value passed is a new slice of type []T
// with a new underlying array whose successive elements are the actual
// arguments". "If the final argument is assignable to a slice type []T and
// is followed by ..., it is passed unchanged as the value for a ...T
// parameter. In this case no new slice is created."
package main

import "cprover"

func sum(xs ...int) int {
	s := 0
	for _, x := range xs {
		s += x
	}
	return s
}

func greet(prefix string, who ...string) (int, bool) { return len(who), who == nil }

type T struct{ n int }

func (t T) add(xs ...int) int { return t.n + sum(xs...) }

type Adder interface{ add(...int) int }

func Max[E int | float64](first E, rest ...E) E {
	m := first
	for _, r := range rest {
		m = max(m, r)
	}
	return m
}

func main() {
	cprover.Assert(sum() == 0 && sum(1, 2, 3) == 6, "sum")
	n, isNil := greet("nobody")
	cprover.Assert(n == 0 && isNil, "nil when no arguments")
	s := []string{"James", "Jasmine"}
	n, isNil = greet("goodbye:", s...)
	cprover.Assert(n == 2 && !isNil, "s...")
	xs := []int{1, 2}
	f := func(ys ...int) { ys[0] = 9 }
	f(xs...)
	cprover.Assert(xs[0] == 9, "same underlying array")
	var a Adder = T{10}
	cprover.Assert(a.add(1, 2) == 13 && T{1}.add() == 1, "methods")
	cprover.Assert(Max(1, 5, 3) == 5 && Max(2.5) == 2.5, "generic")
	g := sum
	cprover.Assert(g(4, 5) == 9, "function value")
}
