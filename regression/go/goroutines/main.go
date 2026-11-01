// Go1.27 §Go_statements: "The function value and parameters are evaluated
// as usual in the calling goroutine, but unlike with a regular call,
// program execution does not wait for the invoked function to complete.
// Instead, the function begins executing independently in a new
// goroutine." Without synchronisation, main may observe any interleaving
// (also lost updates, as x += d is not atomic).
package main

import "cprover"

var x int

func add(d int) { x += d }

func main() {
	d := 1
	go add(d) // d is evaluated now
	d = 2
	go func() { x += 10 }()
	v := x // each read of x may see a different write
	cprover.Assert(v == 0 || v == 1 || v == 10 || v == 11, "possible values")
	cprover.Assert(v != 11, "both goroutines may have run")
}
