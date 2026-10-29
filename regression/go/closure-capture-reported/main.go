// Go1.27 §Function_literals: "Function literals are closures: they may
// refer to variables declared in a surrounding function." Captured
// variables are not supported yet and must be reported, not dropped.
package main

import "cprover"

func main() {
	n := 1
	inc := func() { n++ }
	inc()
	cprover.Assert(n == 2, "captured")
}
