// Go1.27 §Comparison_operators: "Pointers to distinct zero-size variables
// may or may not be equal." Neither outcome may be assumed.
package main

import "cprover"

func main() {
	a, b := new(struct{}), new([0]int)
	c := new(struct{})
	cprover.Assert(a != c, "distinct zero-size variables differ")
	_ = b
}
