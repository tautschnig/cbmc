// Package fmt models the printing functions of the standard library's fmt
// (go1.27) for verification: the formatted text is not modelled, so it is
// an arbitrary string (except for Sprint of a single string, which is
// that string). As fmt does, the functions call the Error or String
// method of each argument that has one (Printf does so for every
// argument, whatever its verb, which includes the cases in which fmt calls
// them), and recover from panics in them. The number of bytes written and
// the error of the functions that write to standard output are arbitrary,
// as they depend on the formatting and on the output; Fprint, Fprintln
// and Fprintf write the text with one call of the writer's Write method,
// as fmt does, and return its results.
//
// The error of Errorf has the types and wrapping of fmt's: errors.New's
// type, *wrapError (one %w verb) wrapping its operand, or *wrapErrors
// (several %w verbs) wrapping their operands that are errors. Which one
// depends on the verbs of the format, which the model does not read (that
// takes a loop over its bytes): for a constant format, the front-end's
// helper reads it and calls errorfKnown instead, with what the format
// determines. Otherwise any of them is possible, and errors.Is may find an
// operand that a %v verb formats.
package fmt

import (
	"cprover"
	"errors"
	"io"
)

type writeError struct{}

func (writeError) Error() string { return "write error" }

func methods(a []any) {
	for _, x := range a {
		call(x)
	}
}

func call(x any) {
	defer func() { recover() }()
	switch v := x.(type) {
	case error:
		_ = v.Error()
	case interface{ String() string }:
		_ = v.String()
	}
}

func result() (n int, err error) {
	n = cprover.Nondet[int]()
	cprover.Assume(n >= 0)
	if cprover.Nondet[bool]() {
		err = writeError{}
	}
	return n, err
}

// text is the formatted text of a, which is not modelled.
func text(a []any) string {
	methods(a)
	return cprover.Nondet[string]()
}

// Print formats using the default formats for its operands and writes to
// standard output.
func Print(a ...any) (n int, err error) {
	methods(a)
	return result()
}

// Println formats using the default formats for its operands and writes to
// standard output.
func Println(a ...any) (n int, err error) {
	methods(a)
	return result()
}

// Printf formats according to a format specifier and writes to standard
// output.
func Printf(format string, a ...any) (n int, err error) {
	methods(a)
	return result()
}

// Sprint formats using the default formats for its operands and returns
// the resulting string.
func Sprint(a ...any) string {
	if len(a) == 1 {
		if s, ok := a[0].(string); ok {
			return s
		}
	}
	return text(a)
}

// Sprintln formats using the default formats for its operands and returns
// the resulting string.
func Sprintln(a ...any) string { return text(a) }

// Sprintf formats according to a format specifier and returns the
// resulting string.
func Sprintf(format string, a ...any) string { return text(a) }

// Fprint formats using the default formats for its operands and writes to
// w.
func Fprint(w io.Writer, a ...any) (n int, err error) {
	return w.Write([]byte(text(a)))
}

// Fprintln formats using the default formats for its operands and writes
// to w.
func Fprintln(w io.Writer, a ...any) (n int, err error) {
	return w.Write([]byte(text(a)))
}

// Fprintf formats according to a format specifier and writes to w.
func Fprintf(w io.Writer, format string, a ...any) (n int, err error) {
	return w.Write([]byte(text(a)))
}

// Append formats using the default formats for its operands, appends the
// result to the byte slice, and returns the updated slice.
func Append(b []byte, a ...any) []byte { return append(b, text(a)...) }

// Appendln formats using the default formats for its operands, appends the
// result to the byte slice, and returns the updated slice.
func Appendln(b []byte, a ...any) []byte { return append(b, text(a)...) }

// Appendf formats according to a format specifier, appends the result to
// the byte slice, and returns the updated slice.
func Appendf(b []byte, format string, a ...any) []byte {
	return append(b, text(a)...)
}

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

func (e *wrapErrors) Error() string { return e.msg }

func (e *wrapErrors) Unwrap() []error { return e.errs }

// Errorf formats according to a format specifier and returns the string as
// a value that satisfies error. With a %w verb, the error wraps the
// operand (see the package documentation).
func Errorf(format string, a ...any) error {
	msg := text(a)
	if len(a) == 0 {
		// Without operands, the text is the format if it has no verbs.
		if cprover.Nondet[bool]() {
			msg = format
		}
		return errors.New(msg)
	}
	switch cprover.Nondet[int]() {
	case 1:
		// One %w verb: its operand, if it is an error.
		i := cprover.Nondet[int]()
		cprover.Assume(0 <= i && i < len(a))
		w := &wrapError{msg: msg}
		w.err, _ = a[i].(error)
		return w
	case 2:
		// Several %w verbs: their operands that are errors.
		var errs []error
		for _, x := range a {
			if e, ok := x.(error); ok && cprover.Nondet[bool]() {
				errs = append(errs, e)
			}
		}
		return &wrapErrors{msg, errs}
	}
	return errors.New(msg)
}

// errorfKnown is Errorf for a format with wraps %w verbs (with
// repetitions), whose operands that the error keeps are the bits of mask,
// and that needs no formatting if plain (see errorfCall in the helper).
func errorfKnown(wraps int, mask uint64, plain bool, format string, a ...any) error {
	if plain {
		return errors.New(format)
	}
	msg := text(a)
	switch wraps {
	case 0:
		return errors.New(msg)
	case 1:
		w := &wrapError{msg: msg}
		for i, x := range a {
			if mask>>i&1 != 0 {
				w.err, _ = x.(error)
			}
		}
		return w
	}
	// One underlying array of the final length (append's capacities vary),
	// so that the elements have one object.
	n := 0
	for i, x := range a {
		if _, ok := x.(error); ok && mask>>i&1 != 0 {
			n++
		}
	}
	var errs []error
	if n > 0 {
		errs = make([]error, 0, n)
	}
	for i, x := range a {
		if e, ok := x.(error); ok && mask>>i&1 != 0 {
			errs = append(errs, e)
		}
	}
	return &wrapErrors{msg, errs}
}
