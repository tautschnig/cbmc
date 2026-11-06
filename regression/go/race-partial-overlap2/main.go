// The data-race check with accesses that overlap partially (through
// unsafe.Pointer): a write of x races with a write of byte 3 of x.
package main

import "unsafe"

var x int64

func main() {
	done := make(chan bool)
	go func() {
		x = 1
		done <- true
	}()
	(*[8]byte)(unsafe.Pointer(&x))[3] = 2
	<-done
}
