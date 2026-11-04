// errors.As with targets of interface and concrete types, through wrapped
// errors.
package main

import (
	"cprover"
	"errors"
	"fmt"
)

type NotFound struct{ Key string }

func (e *NotFound) Error() string { return "not found: " + e.Key }

type Temporary interface{ Temporary() bool }

type timeout struct{}

func (timeout) Error() string   { return "timeout" }
func (timeout) Temporary() bool { return true }

func main() {
	err := fmt.Errorf("lookup: %w", &NotFound{"k"})
	var nf *NotFound
	cprover.Assert(errors.As(err, &nf) && nf.Key == "k", "concrete target")
	var tmp Temporary
	cprover.Assert(!errors.As(err, &tmp) && tmp == nil, "no match")
	joined := errors.Join(errors.New("a"), timeout{})
	cprover.Assert(errors.As(joined, &tmp) && tmp.Temporary(), "interface target")
	var to timeout
	cprover.Assert(errors.As(joined, &to), "value target")
	cprover.Assert(!errors.As(nil, &nf), "nil error")
}
