// errors.Is on an error that has one of two dynamic types, in a program
// with a third error type (with Unwrap() []error): the method calls on it
// (Unwrap, Is) consider only the objects of the types it can have, so that
// symbolic execution does not grow with each level of unwrapping (this ran
// out of memory at --unwind 3).
package main

import (
	"cprover"
	"errors"
)

var ErrNotFound = errors.New("not found")

type wrapError struct {
	msg string
	err error
}

func (e *wrapError) Error() string { return e.msg }
func (e *wrapError) Unwrap() error { return e.err }

type wrapErrors struct {
	msg  string
	errs []error
}

func (e *wrapErrors) Error() string   { return e.msg }
func (e *wrapErrors) Unwrap() []error { return e.errs }

func main() {
	var err error = &wrapError{"x", ErrNotFound}
	other := cprover.Nondet[bool]()
	if other {
		err = errors.New("y")
	}
	var unused error
	if cprover.Nondet[bool]() && false {
		unused = &wrapErrors{"z", nil}
	}
	_ = unused
	cprover.Assert(errors.Is(err, ErrNotFound) == !other, "wrapped")
}
