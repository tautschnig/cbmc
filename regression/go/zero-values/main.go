// Go1.27 §The_zero_value: "When storage is allocated for a variable ...
// and no explicit initialization is provided, the variable or value is
// given a default value": false for booleans, 0 for numeric types.
package main

import "cprover"

var pkgInt int
var pkgBool bool

func main() {
	var i int64
	var b bool
	var u uint8
	cprover.Assert(i == 0 && !b && u == 0, "local zero values")
	cprover.Assert(pkgInt == 0 && !pkgBool, "package-level zero values")
	for k := 0; k < 3; k++ {
		var fresh int
		cprover.Assert(fresh == 0, "zero on every iteration")
		fresh = k + 1
	}
}
