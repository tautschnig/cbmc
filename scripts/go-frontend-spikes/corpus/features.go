// Spike corpus: language features a Go frontend must resolve. Each item
// cites the spec section (Go1.27 §anchor) that defines it.
package main

import (
	"errors"
	"fmt"
	"strings"
)

// Go1.27 §Constants: untyped constants are exact and arbitrary-precision.
// Big overflows int64, but the folded value Small fits.
const Big = 1 << 100
const Small = Big >> 98 // == 4
const Third = 1.0 / 3   // untyped float constant

// Go1.27 §Type_parameter_declarations, §Type_inference.
type Number interface {
	~int | ~int64 | ~float64
}

func Sum[T Number](xs []T) T {
	var s T
	for _, x := range xs {
		s += x
	}
	return s
}

type Pair[K comparable, V any] struct {
	Key K
	Val V
}

// Go1.27 §Struct_types (embedded fields), §Method_sets (promotion).
type Base struct{ ID int }

func (b *Base) Describe() string { return fmt.Sprintf("id=%d", b.ID) }

type Derived struct {
	Base
	Name string
}

// Go1.27 §Interface_types.
type Describer interface{ Describe() string }

type MyInt int

func classify(v any) string {
	// Go1.27 §Type_switches.
	switch x := v.(type) {
	case int:
		return "int"
	case MyInt:
		return "MyInt"
	case Describer:
		return x.Describe()
	default:
		return "other"
	}
}

var ErrNeg = errors.New("negative")

func checked(n int) (int, error) {
	if n < 0 {
		return 0, ErrNeg
	}
	return n * 2, nil
}

func main() {
	// Inference of T = int and T = float64 (Go1.27 §Type_inference).
	a := Sum([]int{1, 2, 3})
	b := Sum([]float64{1.5, 2.5})
	p := Pair[string, int]{Key: "k", Val: Small}

	d := &Derived{Base: Base{ID: 7}, Name: "x"}
	s := d.Describe() // promoted method, implicit &d.Base

	// Closure capturing a (Go1.27 §Function_literals).
	inc := func(k int) int { return a + k }

	m := map[string]int{"a": 1, "b": 2}
	total := 0
	for k, v := range m { // Go1.27 §For_range: unspecified order
		total += len(k) + v
	}

	var x uint8 = 255
	x++ // Go1.27 §Integer_overflow: wraps to 0

	if _, err := checked(-1); errors.Is(err, ErrNeg) {
		total++
	}
	fmt.Println(a, b, p, s, inc(1), total, x, strings.ToUpper("ok"),
		classify(MyInt(3)), classify(d), Third)
}
