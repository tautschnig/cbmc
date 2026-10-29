// Go1.27 §Function_types, §Return_statements: result parameters may be
// named; they are "initialized to the zero values for their type" and a
// "return" without values returns their current values.
package main

import "cprover"

func five() (r int) {
	r = 5
	return
}

func zero() (r int) {
	return
}

func main() {
	cprover.Assert(five() == 5, "bare return of assigned result")
	cprover.Assert(zero() == 0, "named result starts at zero")
}
