// The model of time's timers: a stopped timer does not fire, After and
// AfterFunc fire later, without deadlock.
package main

import (
	"cprover"
	"time"
)

func main() {
	timer := time.NewTimer(time.Second)
	if !timer.Stop() {
		<-timer.C
	}
	start := time.Now()
	<-time.After(time.Millisecond)
	cprover.Assert(time.Since(start) >= time.Millisecond, "fired after")
	done := make(chan bool)
	time.AfterFunc(time.Millisecond, func() { done <- true })
	<-done
}
