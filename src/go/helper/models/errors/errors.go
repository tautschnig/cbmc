// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: reflection replaced by intrinsics of
// package cprover/rt; documentation shortened.

// Package errors models the standard library's package errors (go1.27):
// the same code, with reflection replaced by the front-end's intrinsics.
// As is not modelled (it needs reflection); AsType is.
package errors

import "cprover/rt"

// New returns an error that formats as the given text. Each call to New
// returns a distinct error value even if the text is identical.
func New(text string) error {
	return &errorString{text}
}

type errorString struct {
	s string
}

func (e *errorString) Error() string {
	return e.s
}

// Unwrap returns the result of calling the Unwrap method on err, if err's
// type contains an Unwrap method returning error. Otherwise, Unwrap returns
// nil.
func Unwrap(err error) error {
	u, ok := err.(interface {
		Unwrap() error
	})
	if !ok {
		return nil
	}
	return u.Unwrap()
}

// Is reports whether any error in err's tree matches target.
func Is(err, target error) bool {
	if err == nil || target == nil {
		return err == target
	}

	isComparable := rt.Comparable(target)
	return is(err, target, isComparable)
}

func is(err, target error, targetComparable bool) bool {
	for {
		if targetComparable && err == target {
			return true
		}
		if x, ok := err.(interface{ Is(error) bool }); ok && x.Is(target) {
			return true
		}
		switch x := err.(type) {
		case interface{ Unwrap() error }:
			err = x.Unwrap()
			if err == nil {
				return false
			}
		case interface{ Unwrap() []error }:
			for _, err := range x.Unwrap() {
				if is(err, target, targetComparable) {
					return true
				}
			}
			return false
		default:
			return false
		}
	}
}

// AsType finds the first error in err's tree that matches the type E, and
// if one is found, returns that error value and true. Otherwise, it returns
// the zero value of E and false.
func AsType[E error](err error) (E, bool) {
	if err == nil {
		var zero E
		return zero, false
	}
	var pe *E // lazily initialized
	return asType(err, &pe)
}

func asType[E error](err error, ppe **E) (_ E, _ bool) {
	for {
		if e, ok := err.(E); ok {
			return e, true
		}
		if x, ok := err.(interface{ As(any) bool }); ok {
			if *ppe == nil {
				*ppe = new(E)
			}
			if x.As(*ppe) {
				return **ppe, true
			}
		}
		switch x := err.(type) {
		case interface{ Unwrap() error }:
			err = x.Unwrap()
			if err == nil {
				return
			}
		case interface{ Unwrap() []error }:
			for _, err := range x.Unwrap() {
				if err == nil {
					continue
				}
				if x, ok := asType(err, ppe); ok {
					return x, true
				}
			}
			return
		default:
			return
		}
	}
}
