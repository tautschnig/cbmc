// Recursive calls (Go1.27 §Function_declarations, §Calls).
package main

import "cprover"

func fact(n int) int {
	if n <= 1 {
		return 1
	}
	return n * fact(n-1)
}

func main() {
	cprover.Assert(fact(5) == 120, "5! == 120")
}
