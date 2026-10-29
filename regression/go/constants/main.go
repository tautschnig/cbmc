// Go1.27 §Constants, §Constant_expressions: "Numeric constants represent
// exact values of arbitrary precision and do not overflow"; typed
// constants must be representable in their type.
package main

import "cprover"

const big = 1 << 100
const small = big >> 98
const mostNegative int8 = -128

func main() {
	cprover.Assert(small == 4, "exact intermediate value 2^100")
	var x int8 = mostNegative
	cprover.Assert(x == -128, "typed constant")
	const local = (big - 1) % 7
	cprover.Assert(local == 1, "local constant: (2^100 - 1) mod 7")
}
