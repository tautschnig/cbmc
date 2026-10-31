// The model of package errors: New, Is, Unwrap and AsType, as documented.
// Is compares with the target only if its dynamic type is comparable
// (Go1.27 §Comparison_operators), which the real package determines by
// reflection.
package main

import (
	"cprover"
	"errors"
)

var ErrNotFound = errors.New("not found")

type wrapped struct{ err error }

func (w wrapped) Error() string { return "wrapped" }
func (w wrapped) Unwrap() error { return w.err }

type many struct{ errs []error } // not comparable

func (m many) Error() string   { return "many" }
func (m many) Unwrap() []error { return m.errs }

type codeErr struct{ code int }

func (e *codeErr) Error() string { return "code" }

func main() {
	err := error(wrapped{ErrNotFound})
	cprover.Assert(errors.Is(err, ErrNotFound), "wrapped")
	cprover.Assert(!errors.Is(err, errors.New("not found")), "distinct values")
	cprover.Assert(errors.Unwrap(err) == ErrNotFound &&
		errors.Unwrap(ErrNotFound) == nil, "unwrap")
	tree := error(many{[]error{errors.New("x"), err}})
	cprover.Assert(errors.Is(tree, ErrNotFound), "a tree of errors")
	cprover.Assert(!errors.Is(ErrNotFound, tree), "uncomparable target")
	cprover.Assert(!errors.Is(many{}, many{}), "not compared (it would panic)")
	var e error = wrapped{&codeErr{7}}
	c, ok := errors.AsType[*codeErr](e)
	cprover.Assert(ok && c.code == 7, "AsType")
	_, ok = errors.AsType[*codeErr](ErrNotFound)
	cprover.Assert(!ok, "AsType, no match")
	cprover.Assert(ErrNotFound.Error() == "not found", "message")
}
