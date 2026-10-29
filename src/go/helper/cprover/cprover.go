// Package cprover provides verification primitives for CBMC's Go front-end.
//
// Programs import it as "cprover". The CBMC Go front-end recognises calls to
// these functions and replaces them with the corresponding CBMC primitives;
// the bodies below are what a program gets when it is compiled and run with
// the regular Go toolchain instead (for example, as part of its own tests).
package cprover

// Assert checks that cond holds. CBMC reports a property, described by
// description (which must be a constant string), that fails if cond can be
// false. When run with the Go toolchain, a false cond panics.
func Assert(cond bool, description string) {
	if !cond {
		panic("assertion failed: " + description)
	}
}

// Assume restricts verification to executions in which cond holds. When run
// with the Go toolchain, a false cond panics.
func Assume(cond bool) {
	if !cond {
		panic("assumption violated")
	}
}

// Nondet returns an arbitrary value of type T: CBMC considers every value
// of T. When run with the Go toolchain, it returns the zero value of T.
func Nondet[T any]() T {
	var zero T
	return zero
}
