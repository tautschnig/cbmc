// Go1.27 §Run_time_panics: "Execution errors such as attempting to index an
// array out of bounds trigger a run-time panic equivalent to a call of the
// built-in function panic with a value of the implementation-defined
// interface type runtime.Error. That type satisfies the predeclared
// interface type error." §Handling_panics: "If D returns normally, without
// starting a new panic, the panicking sequence stops." A recovered panic is
// not a property violation.
package main

import "cprover"

func div(a, b int) (q int, err error) {
	defer func() {
		if r := recover(); r != nil {
			err = r.(error)
			q = -1
		}
	}()
	return a / b, nil
}

func deref(p *int) (v int, err error) {
	defer func() { err, _ = recover().(error) }()
	return *p, nil
}

func index(s []int, i int) (v int, ok bool) {
	defer func() {
		if recover() != nil {
			ok = false
		}
	}()
	return s[i], true
}

// The protect pattern: callers are protected from panics in g.
func protect[T any](g func() T) (v T, ok bool) {
	defer func() {
		if x := recover(); x != nil {
			ok = false
		}
	}()
	return g(), true
}

func main() {
	b := cprover.Nondet[int]()
	q, err := div(7, b)
	if b == 0 {
		cprover.Assert(q == -1 && err != nil, "recovered")
		cprover.Assert(err.Error() == "runtime error: integer divide by zero",
			"gc's message")
	} else {
		cprover.Assert(err == nil && q == 7/b, "no panic")
	}

	var p *int
	_, err = deref(p)
	cprover.Assert(err != nil, "nil dereference recovered")

	_, ok := index([]int{1}, 3)
	cprover.Assert(!ok, "index out of range recovered")

	v, ok := protect(func() int { return 4 })
	cprover.Assert(v == 4 && ok, "protect, no panic")
	_, ok = protect(func() int { var m map[int]int; m[0] = 1; return 1 })
	cprover.Assert(!ok, "protect, panic")
}
