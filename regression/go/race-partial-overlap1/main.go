// The data-race check with accesses that overlap partially (through
// unsafe.Pointer): a write of byte 3 of x races with a write of x.
package main

import "unsafe"

var x int64

func main() {
	done := make(chan bool)
	go func() {
		(*[8]byte)(unsafe.Pointer(&x))[3] = 1
		done <- true
	}()
	x = 2
	<-done
}
