// The model of fmt: Errorf wraps the operands of its %w verbs, as fmt does
// (the helper reads constant formats); Fprintf writes once; the String
// methods of operands are called.
package main

import (
	"cprover"
	"errors"
	"fmt"
)

var ErrNotFound = errors.New("not found")

type counter struct{ n int }

func (c *counter) Write(p []byte) (int, error) { c.n++; return len(p), nil }

type named struct{ calls *int }

func (v named) String() string { *v.calls++; return "named" }

func find(k int) error {
	if k < 0 {
		return fmt.Errorf("find %d: %w", k, ErrNotFound)
	}
	return nil
}

func main() {
	e2 := fmt.Errorf("plain")
	err := find(-1)
	cprover.Assert(err != nil, "an error")
	// gc: the error wraps ErrNotFound.
	cprover.Assert(errors.Is(err, ErrNotFound), "wraps ErrNotFound")
	cprover.Assert(errors.Unwrap(err) == ErrNotFound, "unwrap")
	e3 := fmt.Errorf("%v and %w", ErrNotFound, err)
	cprover.Assert(errors.Unwrap(e3) == err, "only the %w operand")
	e4 := fmt.Errorf("%w, %w", err, e2)
	cprover.Assert(errors.Unwrap(e4) == nil && errors.Is(e4, e2) &&
		errors.Is(e4, ErrNotFound), "several %w")
	cprover.Assert(e2.Error() == "plain", "text of a plain Errorf")
	cprover.Assert(fmt.Sprint("s") == "s", "Sprint of a string")
	var c counter
	n, werr := fmt.Fprintf(&c, "%d", 3)
	cprover.Assert(c.n == 1 && werr == nil && n >= 0, "one Write")
	calls := 0
	_ = fmt.Sprintf("%v", named{&calls})
	cprover.Assert(calls == 1, "String called")
	cprover.Assert(errors.Unwrap(e2) == nil, "no wrapping")
}
