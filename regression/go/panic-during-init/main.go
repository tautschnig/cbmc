// Go1.27 §Handling_panics: a panic that is not recovered terminates the
// program, also during §Package_initialization: main is not invoked.
package main

import "cprover"

var x = start()

func start() int {
	panic("initialisation failed")
}

func main() {
	cprover.Assert(false, "main is not invoked")
	_ = x
}
