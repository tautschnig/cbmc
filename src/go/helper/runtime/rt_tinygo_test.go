//go:build tinygo

package rt

// Checks of the capacities of append that the front-end uses for TinyGo
// (GrowCapTinyGo) against the TinyGo that runs these tests (`tinygo test`,
// with the default optimisation and with -opt=0), so that a change of
// TinyGo's behaviour shows up. rt_test.go has the checks for gc.

import (
	"math/bits"
	"testing"
)

var sink any

type pair struct {
	a int32
	b byte
}

type ptrBytes struct {
	p *int
	a [100]byte
}

// grown returns the capacity of a heap-allocated slice of length oldLen
// and capacity oldCap after appending until length newLen, in one append
// of newLen-oldLen values or with single appends.
func grown[T any](oldLen, oldCap, newLen int, single bool) int {
	s := make([]T, oldLen, oldCap)
	sink = s
	if single {
		var z T
		s = append(s, z)
	} else {
		s = append(s, make([]T, newLen-oldLen)...)
	}
	sink = s
	return cap(s)
}

// Slices that do not escape, or only by being returned.

func localCap[T any](n int) int {
	var s []T
	var z T
	for i := 0; i < n; i++ {
		s = append(s, z)
	}
	return cap(s)
}

func returned[T any](n int) []T {
	var s []T
	var z T
	for i := 0; i < n; i++ {
		s = append(s, z)
	}
	return s
}

func observed[T any](n int) []int {
	var s []T
	var caps []int
	var z T
	for i := 0; i < n; i++ {
		s = append(s, z)
		caps = append(caps, cap(s))
	}
	return caps
}

func check[T any](t *testing.T, name string) {
	caps := []int{0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 100,
		255, 256, 257, 300, 511, 512, 513, 1000, 1024, 4096}
	for _, oldCap := range caps {
		if got, want := grown[T](oldCap, oldCap, oldCap+1, true),
			GrowCapTinyGo(oldCap+1); got != want {
			t.Errorf("%s: append(len %d, cap %d) one value: cap %d, model %d",
				name, oldCap, oldCap, got, want)
		}
		for _, oldLen := range []int{0, oldCap / 2, oldCap} {
			for _, newLen := range []int{oldCap + 1, oldCap + 2, 2 * oldCap,
				2*oldCap + 1, 3*oldCap + 5, oldCap + 1000} {
				if newLen <= oldCap {
					continue
				}
				got := grown[T](oldLen, oldCap, newLen, false)
				if want := GrowCapTinyGo(newLen); got != want {
					t.Errorf("%s: append(len %d, cap %d) to len %d: cap %d, model %d",
						name, oldLen, oldCap, newLen, got, want)
				}
			}
		}
	}
	// Without a stack buffer as gc's, the capacity is the same for slices
	// that do not escape.
	for n := 1; n <= 70; n++ {
		want := 0
		for i := 1; i <= n; i++ {
			if i > want {
				want = GrowCapTinyGo(i)
			}
		}
		if c := localCap[T](n); c != want {
			t.Errorf("%s: %d appends to a local slice: cap %d, model %d",
				name, n, c, want)
		}
		if c := cap(returned[T](n)); c != want {
			t.Errorf("%s: %d appends to a returned slice: cap %d, model %d",
				name, n, c, want)
		}
		if c := observed[T](n)[n-1]; c != want {
			t.Errorf("%s: %d appends, capacity observed: cap %d, model %d",
				name, n, c, want)
		}
	}
}

// TestGrowCapTinyGo checks GrowCapTinyGo for elements of several sizes,
// with and without pointers.
func TestGrowCapTinyGo(t *testing.T) {
	check[byte](t, "byte")
	check[int16](t, "int16")
	check[int64](t, "int64")
	check[[3]byte](t, "[3]byte")
	check[pair](t, "pair")
	check[[40]byte](t, "[40]byte")
	check[struct{}](t, "struct{}")
	check[*int](t, "*int")
	check[string](t, "string")
	check[[]int](t, "[]int")
	check[any](t, "any")
	check[ptrBytes](t, "ptrBytes")

	// Appending a string to a []byte.
	b := append([]byte("ab"), "cdefg"...)
	sink = b
	if want := GrowCapTinyGo(7); cap(b) != want {
		t.Errorf("append([]byte, string...): cap %d, model %d", cap(b), want)
	}
}

// TestBitLen checks bitLen against math/bits.Len.
func TestBitLen(t *testing.T) {
	for _, x := range []uint{0, 1, 2, 3, 4, 7, 8, 255, 256, 1<<31 - 1, 1 << 31,
		^uint(0), ^uint(0) >> 1} {
		if got, want := bitLen(x), bits.Len(x); got != want {
			t.Errorf("bitLen(%d) = %d, want %d", x, got, want)
		}
	}
}
