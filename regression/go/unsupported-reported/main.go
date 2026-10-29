// An unsupported construct must be reported as an error: silently dropping
// it could make verification succeed vacuously.
package main

import "cprover"

func main() {
	s := "not yet"
	cprover.Assert(len(s) == 7, "length")
}
