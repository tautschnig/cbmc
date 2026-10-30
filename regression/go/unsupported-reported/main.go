// An unsupported construct must be reported as an error: silently dropping
// it could make verification succeed vacuously.
package main

import "cprover"

func main() {
	done := false
	go func() {}() // goroutines are not supported yet
	cprover.Assert(!done, "not done")
}
