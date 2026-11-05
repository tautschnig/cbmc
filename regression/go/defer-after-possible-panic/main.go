// Go1.27 §Defer_statements: deferred calls run when the function returns
// or panics, but only those whose defer statements executed. Where a panic
// may occur before a defer statement (here, in growing the buffer for an
// arbitrary amount of data), the call must still go to the function that
// was deferred, rather than to any function of its type.
package main

import (
	"bytes"
	"cprover"
	"io"
	"sync"
)

var count int

func put(p *[]byte) { count++ }

type L struct {
	mu sync.Mutex
	w  io.Writer
}

func (l *L) out(n int) {
	buf := new([]byte)
	defer put(buf)
	*buf = append(*buf, make([]byte, n)...)
	l.mu.Lock()
	defer l.mu.Unlock()
	l.w.Write(*buf)
}

func main() {
	var buf bytes.Buffer
	n := cprover.Nondet[int]()
	cprover.Assume(n >= 0 && n < 100)
	l := &L{w: &buf}
	l.out(n)
	cprover.Assert(count == 1, "the deferred call ran once")
	cprover.Assert(buf.Len() == n, "and the data was written")
	l.mu.Lock() // not locked: the deferred Unlock ran
}
