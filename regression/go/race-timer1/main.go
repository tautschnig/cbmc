// The model of timers allocates values (of run-time errors, which recover
// makes observable) in its atomic sections, which are not shared before the
// section ends: their accesses are not announced (where their pointers are
// not yet assigned), so the race check is complete.
package main

import "time"

func main() {
	defer func() { _ = recover() }()
	var x int
	_ = x
	timer := time.AfterFunc(10, func() {
		x = 1
	})
	defer timer.Stop()
}
