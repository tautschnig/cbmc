// Go1.27 §Defer_statements: "Calls of built-in functions are restricted
// as for expression statements." "Each time a "defer" statement executes,
// the function value and parameters to the call are evaluated as usual and
// saved anew but the actual function is not invoked."
package main

import "cprover"

func copyLater(dst, src []int) {
	defer copy(dst, src) // the slices are saved now, copied at return
	src[0] = 9
	dst = nil
}

func clearLater(m map[string]int, s []int) {
	defer clear(m)
	defer clear(s[:2]) // s[:2] is evaluated now
	s = s[2:]
	m["b"] = 2
}

func panicLater(v int) (got any) {
	defer func() { got = recover() }()
	defer panic(v) // v is evaluated now
	v++
	return nil
}

func printLater(n int) int {
	defer println("n =", n)
	defer print(n, "\n")
	return n + 1
}

func main() {
	dst := []int{0, 0}
	copyLater(dst, []int{1, 2, 3})
	cprover.Assert(dst[0] == 9 && dst[1] == 2, "copy at return, from src then")

	m := map[string]int{"a": 1}
	s := []int{1, 2, 3}
	clearLater(m, s)
	cprover.Assert(len(m) == 0, "clear of a map at return")
	cprover.Assert(s[0] == 0 && s[1] == 0 && s[2] == 3, "of the saved slice")

	cprover.Assert(panicLater(4) == 4, "panic with the value saved")
	cprover.Assert(printLater(1) == 2, "print does not change results")
}
