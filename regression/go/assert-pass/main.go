// cprover.Assert on a property that holds.
package main

import "cprover"

func main() {
	x := 2
	cprover.Assert(x+x == 4, "two plus two")
}
