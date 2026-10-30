// Go1.27 §Method_declarations: "If the method declaration specifies type
// parameters (possibly in addition to type parameters declared by the
// receiver specification), the method name denotes a generic method [Go
// 1.27]. Like a generic function, a generic method must be instantiated
// before it can be called or used as a value." With the spec's example:
//
//	type List[E any] []E
//
//	// Apply returns the list obtained from applying f to each element of l.
//	func (l List[E]) Apply[F any](f func(E) F) List[F] {
package main

import "cprover"

type List[E any] []E

func (l List[E]) Apply[F any](f func(E) F) List[F] {
	r := make(List[F], len(l))
	for i, x := range l {
		r[i] = f(x)
	}
	return r
}

type Counter struct{ n int }

// A generic method of a non-generic type, with a pointer receiver.
func (c *Counter) Add[T ~int | ~int8](x T) {
	c.n += int(x)
}

func main() {
	l := List[int]{1, 2}
	strs := l.Apply(func(x int) string { return "s" }) // F inferred
	cprover.Assert(len(strs) == 2 && strs[1] == "s", "inferred")
	halves := l.Apply[float64](func(x int) float64 { return float64(x) / 2 })
	cprover.Assert(halves[0] == 0.5 && halves[1] == 1, "explicit")
	apply := l.Apply[bool] // a method value of an instance
	l = List[int]{2, 3}
	odd := apply(func(x int) bool { return x%2 == 1 })
	cprover.Assert(odd[0] && !odd[1], "method value binds the receiver")

	var c Counter
	c.Add(2)
	c.Add(int8(3))
	cprover.Assert(c.n == 5, "generic method with pointer receiver")
}
