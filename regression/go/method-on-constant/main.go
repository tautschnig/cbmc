// Go1.27 §Method_sets, §Selectors: methods of typed constants, called,
// as method values and through method expressions.
package main

import "cprover"

type Color int

const Red Color = 2

func (c Color) Double() int { return int(c) * 2 }

func main() {
	cprover.Assert(Red.Double() == 4, "method on a constant")
	f := Red.Double
	cprover.Assert(f() == 4, "method value of a constant")
	cprover.Assert(Color.Double(Red) == 4, "method expression")
}
