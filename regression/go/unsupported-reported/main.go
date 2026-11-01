// An unsupported construct must be reported as an error: silently dropping
// it could make verification succeed vacuously.
package main

import "cprover"

func main() {
	done := false
	seq := func(yield func(int) bool) {}
	for range seq { // range over functions is not supported yet
		done = true
	}
	cprover.Assert(!done, "not done")
}
