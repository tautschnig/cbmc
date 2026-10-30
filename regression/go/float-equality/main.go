// Go1.27 §Comparison_operators: floating-point values are compared as
// defined by IEEE 754, also inside structs and arrays ("Two struct values
// are equal if their corresponding non-blank field values are equal") and
// as map keys (§Map_types: keys are compared with ==).
package main

import "cprover"

type P struct{ x, y float64 }

func main() {
	zero := 0.0
	nan := zero / zero
	cprover.Assert(P{zero, 1} == P{-zero, 1}, "-0 == +0 in structs")
	cprover.Assert(P{nan, 1} != P{nan, 1}, "NaN != NaN in structs")
	cprover.Assert([2]float64{nan, 1} != [2]float64{nan, 1}, "and in arrays")
	m := map[float64]int{}
	m[nan] = 1
	m[nan] = 2
	m[zero] = 3
	m[-zero] = 4
	cprover.Assert(len(m) == 3 && m[zero] == 4 && m[nan] == 0, "map keys")
}
