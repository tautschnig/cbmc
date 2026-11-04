// Package lib has functions and methods that the program calls (one only
// through an interface), and ones that it cannot reach, which use a
// construct the front-end does not support.
package lib

import "os"

// Twice returns 2*x.
func Twice(x int) int { return 2 * x }

// Home is not reached by the program.
func Home() string { return os.Getenv("HOME") }

// Disc is a disc of radius R.
type Disc struct{ R int }

// Area is about the area of d, called through an interface.
func (d Disc) Area() int { return 3 * d.R * d.R }

// Env is not reached by the program.
func (d Disc) Env() string { return os.Getenv("DISC") }
