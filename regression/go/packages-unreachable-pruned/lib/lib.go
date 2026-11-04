// Package lib has a function that the program calls and one that it
// cannot reach, which uses a construct the front-end does not support.
package lib

import "os"

// Twice returns 2*x.
func Twice(x int) int { return 2 * x }

// Home is not reached by the program.
func Home() string { return os.Getenv("HOME") }
