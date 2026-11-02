// An unsupported construct must be reported as an error: silently dropping
// it could make verification succeed vacuously.
package main

import "cprover"

func main() {
	msg := "not constant"
	cprover.Assert(false, msg) // descriptions must be constants
}
