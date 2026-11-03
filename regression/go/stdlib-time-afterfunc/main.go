// The model of time's timers: the function of AfterFunc runs in a
// goroutine of its own, at a later time.
package main

import (
	"cprover"
	"time"
)

func main() {
	start := time.Now()
	done := make(chan time.Duration)
	time.AfterFunc(time.Millisecond, func() { done <- time.Since(start) })
	cprover.Assert(<-done >= time.Millisecond, "after the duration")
}
