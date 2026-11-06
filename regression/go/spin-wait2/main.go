// A spin-wait loop on a plain variable races with the write that ends it.
package main

import "runtime"

func main() {
	done := false
	go func() {
		done = true
	}()
	for !done {
		runtime.Gosched()
	}
}
