// Package fmt models the printing functions of the standard library's fmt
// (go1.27) for verification: the output is not modelled. As fmt does, they
// call the Error or String method of each argument that has one (Printf
// does so for every argument, whatever its verb, which includes the cases
// in which fmt calls them), and recover from panics in them. The number of
// bytes written and the error are arbitrary, as they depend on the
// formatting and on the output.
package fmt

import "cprover"

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
