package geom

import "example.com/shapes/internal/counter"

type Shape interface {
	Area() int
}

type Rect struct{ W, H int }

func (r Rect) Area() int { counter.Inc(); return r.W * r.H }

var Unit = Rect{1, 1}

func Max[T int | float64](a, b T) T {
	if a > b {
		return a
	}
	return b
}
