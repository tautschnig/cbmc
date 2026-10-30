// Go1.27 §Conversions: "When converting a floating-point number to an
// integer, the fraction is discarded (truncation towards zero). When
// converting an integer or floating-point number to a floating-point type,
// or a complex number to another complex type, the result value is rounded
// to the precision specified by the destination type." §Constants:
// constants are exact; a typed constant is rounded to its type.
package main

import "cprover"

func main() {
	v := 3.9
	cprover.Assert(int(v) == 3 && int(-v) == -3 && uint8(v) == 3,
		"truncation towards zero")
	cprover.Assert(int64(-0.5+v-v) == 0, "small negatives truncate to zero")

	var i int64 = 1<<53 + 1
	cprover.Assert(float64(i) == 1<<53, "rounding to nearest, ties to even")
	i = 1<<53 + 3
	cprover.Assert(float64(i) == 1<<53+4, "ties to even, upwards")

	d := 1.0000001
	cprover.Assert(float32(d) == 1.0000001 && float64(float32(d)) != d,
		"float64 to float32 rounds")
	var big float64 = 1e39
	cprover.Assert(float32(big) > 3.4e38, "overflow to +Inf")

	const third = 1.0 / 3 // exact
	cprover.Assert(third*3 == 1, "constant arithmetic is exact")
	x := 1.0
	cprover.Assert(x/3*3 == 1, "1/3*3 happens to round to 1")
	y := 0.1
	cprover.Assert(y*10 == 1 && y+y+y+y+y+y+y+y+y+y != 1, "accumulated error")
}
