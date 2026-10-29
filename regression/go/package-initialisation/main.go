// Go1.27 §Package_initialization: package-level variables are initialised
// in dependency order, then "all init functions in the order they appear
// in the source". §Program_execution: then main.main is invoked.
package main

import "cprover"

var a = b + c // depends on b and c, which are declared later
var b = f()
var c = 10
var stage int

func f() int { return c * 2 } // depends on c

func init() {
	cprover.Assert(stage == 0, "first init runs first")
	stage = 1
}

func init() {
	cprover.Assert(stage == 1, "second init runs second")
	stage = 2
}

func main() {
	cprover.Assert(c == 10 && b == 20 && a == 30, "dependency order")
	cprover.Assert(stage == 2, "init functions ran before main")
}
