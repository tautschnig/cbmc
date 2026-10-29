// With --function, CBMC calls the given function of package main with
// arbitrary arguments (after package initialisation).
package main

import "cprover"

func check(x int) {
	cprover.Assert(x != 42, "x is not 42")
}

func main() {}
