// Go1.27 §Defer_statements: "Calls of built-in functions are restricted as
// for expression statements." Deferred calls of built-in functions are not
// supported yet, and reported.
package main

func main() {
	m := map[int]int{1: 1}
	defer delete(m, 1)
}
