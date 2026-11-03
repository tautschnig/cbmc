// Go1.27 §Qualified_identifiers: a function of another package as a
// function value (here of a modelled standard-library package).
package main

import (
	"cprover"
	"errors"
)

func apply(f func(string) error, s string) error { return f(s) }

func main() {
	newError := errors.New
	err := apply(newError, "x")
	cprover.Assert(err != nil && err.Error() == "x", "errors.New as a value")
}
