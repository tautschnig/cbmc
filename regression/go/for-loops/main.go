// Go1.27 §For_statements, §For_clause: the post statement runs after each
// execution of the body, including after "continue"; a missing condition
// is equivalent to true. §Break_statements, §Continue_statements.
package main

import "cprover"

func main() {
	sum := 0
	for i := 0; i < 10; i++ {
		if i%2 == 0 {
			continue // still increments i
		}
		sum += i
	}
	cprover.Assert(sum == 25, "sum of odd numbers below 10")

	n := 0
	for n < 5 {
		n++
	}
	cprover.Assert(n == 5, "condition-only loop")

	k := 0
	for {
		k++
		if k == 7 {
			break
		}
	}
	cprover.Assert(k == 7, "infinite loop with break")
}
