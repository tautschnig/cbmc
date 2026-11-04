// Constant operands in parentheses of index expressions and method calls
// (the helper sends the value of a constant expression, not its operands).
package main

import "cprover"

const digits = "0123456789"

type Celsius int

func (c Celsius) Double() Celsius { return 2 * c }

func main() {
	i := cprover.Nondet[int]()
	cprover.Assume(i >= 0 && i < 10)
	d := (digits)[i]
	cprover.Assert(d == '0'+byte(i), "index of a parenthesised constant")
	cprover.Assert((Celsius(21)).Double() == 42, "method of a parenthesised constant")
}
