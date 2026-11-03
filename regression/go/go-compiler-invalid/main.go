// Without --go-compiler, the capacities of append follow the compiler of
// the go command on PATH, as for C the front-end follows the configured
// compiler: gc go1.27 doubles the capacity here (Go1.27
// §Appending_and_copying_slices leaves it open).
package main

import "cprover"

func main() {
	s := append(make([]int, 4), 1)
	cprover.Assert(cap(s) == 8, "gc doubles")
}
