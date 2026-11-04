// Go1.27 §Defer_statements: "Calls of built-in functions are restricted as
// for expression statements." Deferred calls of built-in functions other
// than delete and close are not supported yet, and reported.
package main

func main() {
	dst := make([]int, 1)
	defer copy(dst, []int{1})
}
