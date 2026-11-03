package main

import (
	"errors"
	"fmt"
	"testing"
)

// wrappedBy returns the operands (by index) that the error of fmt.Errorf
// wraps, and how: 0 none, 1 Unwrap() error, 2 Unwrap() []error.
func wrappedBy(err error, a []any) (shape int, wrapped []int) {
	index := func(e error) int {
		for i, x := range a {
			if x == any(e) {
				return i
			}
		}
		return -1
	}
	switch u := err.(type) {
	case interface{ Unwrap() error }:
		if e := u.Unwrap(); e != nil {
			wrapped = append(wrapped, index(e))
		}
		return 1, wrapped
	case interface{ Unwrap() []error }:
		for _, e := range u.Unwrap() {
			wrapped = append(wrapped, index(e))
		}
		return 2, wrapped
	}
	return 0, nil
}

// TestErrorfWraps checks errorfWraps, which the helper uses for calls of
// fmt.Errorf with constant formats, against fmt.Errorf.
func TestErrorfWraps(t *testing.T) {
	e1, e2, e3 := errors.New("1"), errors.New("2"), errors.New("3")
	formats := []string{
		"plain", "%w", "%v", "x: %w", "%d: %w", "%w %w", "%w %v %w",
		"%v %w", "%[2]w %[1]w", "%[1]w %[1]w", "%[2]w", "%*d %w", "%.*d %w",
		"%5.2f %w", "%-+# 0w", "%%w %w", "%[3]*.[2]*[1]f %w", "%w%", "%!w",
		"%[5]w %w", "%[2]d %w", "%w %w %w %w", "%s %q %x %w", "%[1]v %w",
		"%[0]w %w", "%[x]w %w", "%[2]5w %w", "%w %[1]w", "%ŵ %w", "%",
	}
	args := [][]any{{}, {e1}, {1, e1}, {e1, e2}, {e1, 2, e3}, {e1, e2, e3},
		{"s", e1}, {3, 2, 1.5, e1}}
	for _, format := range formats {
		for _, a := range args {
			err := fmt.Errorf(format, a...)
			shape, want := wrappedBy(err, a)
			wrapped, reordered := errorfWraps(format, len(a))
			if reordered {
				sortInts(wrapped)
			}
			gotShape := 0
			switch {
			case len(wrapped) == 1:
				gotShape = 1
			case len(wrapped) > 1:
				gotShape = 2
			}
			var got []int
			seen := map[int]bool{}
			for _, i := range wrapped {
				if seen[i] {
					continue
				}
				seen[i] = true
				if _, ok := a[i].(error); ok {
					got = append(got, i)
				}
			}
			if gotShape != shape || fmt.Sprint(got) != fmt.Sprint(want) {
				t.Errorf("Errorf(%q) with %d operands: wraps %v as %d, "+
					"errorfWraps gives %v as %d", format, len(a), want,
					shape, got, gotShape)
			}
		}
	}
}

func sortInts(xs []int) {
	for i := range xs {
		for j := i + 1; j < len(xs); j++ {
			if xs[j] < xs[i] {
				xs[i], xs[j] = xs[j], xs[i]
			}
		}
	}
}
