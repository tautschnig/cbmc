// Type errors are reported by go/types with file:line:col positions, and
// the program is not verified.
package main

func main() {
	var x int
	x = "s" + ""
	_ = x
}
