// Go1.27 §Conversions: "In all non-constant conversions involving
// floating-point or complex values, if the result type cannot represent the
// value the conversion succeeds but the result value is
// implementation-dependent." No particular result may be assumed (gc gives
// math.MinInt64 for this on amd64, other implementations may differ).
package main

import "cprover"

func main() {
	f := 1e30
	i := int64(f)
	cprover.Assert(i == -1<<63, "an implementation-dependent value")
	zero := 0.0
	nan := zero / zero
	j := int32(nan)
	cprover.Assert(j == 0, "NaN converts to an implementation-dependent value")
	w := 2147483647.9
	k := int32(w)
	cprover.Assert(k == 2147483647, "in range: truncated")
	m := -128.9
	l := int8(m)
	cprover.Assert(l == -128, "in range: truncated towards zero")
}
