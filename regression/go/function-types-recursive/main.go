// Go1.27 §Function_types, §Type_declarations: named function types that
// refer to themselves, directly, mutually, or through parameters.

package main

import "cprover"

type lexer struct{ n int }
type stateFn func(*lexer) stateFn

func a(l *lexer) stateFn {
	l.n++
	if l.n < 3 {
		return a
	}
	return nil
}

// Mutual recursion through two named types.
type A func() B
type B func() A

func fa() B { return fb }
func fb() A { return nil }

// A method on a recursive function type.
func (f stateFn) step(l *lexer) stateFn { return f(l) }

// A recursive type through a parameter.
type visitor func(v visitor, depth int) int

func walk(v visitor, depth int) int {
	if depth == 0 {
		return 0
	}
	return 1 + v(v, depth-1)
}

func main() {
	l := &lexer{}
	for s := stateFn(a); s != nil; {
		s = s.step(l)
	}
	cprover.Assert(l.n == 3, "state machine runs three steps")

	var b B = fa()
	cprover.Assert(b != nil, "fa returns fb")
	cprover.Assert(b() == nil, "fb returns nil")

	cprover.Assert(walk(walk, 3) == 3, "walk")

	var i interface{} = stateFn(a)
	_, ok := i.(stateFn)
	cprover.Assert(ok, "dynamic type stateFn")

	lit := func(l *lexer) stateFn { return nil }
	var s stateFn = lit
	cprover.Assert(s(l) == nil, "literal")

	var z stateFn
	defer func() {
		r := recover()
		cprover.Assert(r != nil, "calling nil panics")
		cprover.Assert(l.n == 3, "the deferred call did not run")
	}()
	defer z(l)
}
