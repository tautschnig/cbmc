// Types of standard-library packages without models (here io.PipeReader,
// which has a sync.Mutex field) refer to the models of sync, not to the
// real types: both are named sync.Mutex, and the model's methods use the
// model's fields.
package main

import (
	"cprover"
	"io"
	"sync"
)

func pipe() *io.PipeReader {
	r, _ := io.Pipe() // without a model: fails a property where reached
	return r
}

func main() {
	var r *io.PipeReader
	if cprover.Nondet[bool]() && false {
		r = pipe()
	}
	var mu sync.Mutex
	n := 0
	mu.Lock()
	n++
	mu.Unlock()
	cprover.Assert(n == 1 && r == nil, "locked and unlocked")
}
