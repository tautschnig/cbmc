package rt

// Checks of the capacities of append that the front-end uses (GrowCapGC,
// StackCapGC, RoundUpCapGC) against the gc that runs these tests, so that a
// change of gc's behaviour shows up in CI. Run with the default flags and
// with -gcflags=all=-N (no optimisation, hence no stack allocation).

import (
	"fmt"
	"testing"
	"unsafe"
)

var sink any

// elem describes an element type: its size and whether it has pointers
// (as the helper reports them for slice types, see hasPointers in
// main.go).
type elem struct {
	name     string
	size     int
	pointers bool
}

func elemOf[T any](pointers bool) elem {
	var z T
	return elem{fmt.Sprintf("%T", z), int(unsafe.Sizeof(z)), pointers}
}

type ptrBytes struct {
	p *int
	a [100]byte
}

type ptrLarge struct {
	p *int
	a [520]byte
}

type pair struct {
	a int32
	b byte
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

func checkGrown[T any](t *testing.T, pointers bool) {
	e := elemOf[T](pointers)
	caps := []int{0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 100,
		255, 256, 257, 300, 511, 512, 513, 1000, 1024, 4096, 10000}
	for _, oldCap := range caps {
		if e.size*oldCap > 1<<22 {
			continue
		}
		got := grown[T](oldCap, oldCap, oldCap+1, true)
		if want := GrowCapGC(oldCap+1, oldCap, e.size, pointers); got != want {
			t.Errorf("%s: append(len %d, cap %d) one value: cap %d, model %d",
				e.name, oldCap, oldCap, got, want)
		}
		for _, oldLen := range []int{0, oldCap / 2, oldCap} {
			for _, newLen := range []int{oldCap + 1, oldCap + 2, 2 * oldCap,
				2*oldCap + 1, 3*oldCap + 5, oldCap + 1000} {
				if newLen <= oldCap {
					continue
				}
				got := grown[T](oldLen, oldCap, newLen, false)
				want := GrowCapGC(newLen, oldCap, e.size, pointers)
				if got != want {
					t.Errorf("%s: append(len %d, cap %d) to len %d: cap %d, model %d",
						e.name, oldLen, oldCap, newLen, got, want)
				}
			}
		}
	}
}

// TestGrowCapGC checks GrowCapGC against slices on the heap, for which gc
// always calls runtime.growslice.
func TestGrowCapGC(t *testing.T) {
	checkGrown[byte](t, false)
	checkGrown[int16](t, false)
	checkGrown[int32](t, false)
	checkGrown[int64](t, false)
	checkGrown[[3]byte](t, false)
	checkGrown[pair](t, false)
	checkGrown[[12]byte](t, false)
	checkGrown[[5]int32](t, false)
	checkGrown[[40]byte](t, false)
	checkGrown[[600]byte](t, false)
	checkGrown[struct{}](t, false)
	checkGrown[*int](t, true)
	checkGrown[string](t, true)
	checkGrown[[]int](t, true)
	checkGrown[any](t, true)
	checkGrown[func()](t, true)
	checkGrown[map[int]int](t, true)
	checkGrown[ptrBytes](t, true)
	checkGrown[ptrLarge](t, true)
	checkGrown[[0]*int](t, false)
}

// next returns the capacities the model allows after appending one value
// to a slice of length oldLen and capacity oldCap.
func next(e elem, oldLen, oldCap int) []int {
	newLen := oldLen + 1
	if newLen <= oldCap {
		return []int{oldCap}
	}
	caps := []int{GrowCapGC(newLen, oldCap, e.size, e.pointers)}
	if k := StackCapGC(e.size); k != 0 && newLen <= k {
		if oldLen == 0 {
			caps = append(caps, k)
		}
		caps = append(caps, RoundUpCapGC(newLen, e.size, e.pointers))
	}
	return caps
}

// reachable returns the capacities the model allows after n appends of
// one value each to a nil slice.
func reachable(e elem, n int) map[int]bool {
	caps := map[int]bool{0: true}
	for i := 0; i < n; i++ {
		nextCaps := map[int]bool{}
		for c := range caps {
			for _, d := range next(e, i, c) {
				nextCaps[d] = true
			}
		}
		caps = nextCaps
	}
	return caps
}

// Slices that do not escape, or only by being returned, which gc may back
// by a buffer on the stack.

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

func observed[T any](n int) ([]T, []int) {
	var s []T
	var caps []int
	var z T
	for i := 0; i < n; i++ {
		s = append(s, z)
		caps = append(caps, cap(s))
	}
	return s, caps
}

func localCapInt(n int) int {
	var s []int
	for i := 0; i < n; i++ {
		s = append(s, i)
	}
	return cap(s)
}

func returnedBytes(n int) []byte {
	var s []byte
	for i := 0; i < n; i++ {
		s = append(s, byte(i))
	}
	return s
}

func observedInt(n int) ([]int, []int) {
	var s []int
	var caps []int
	for i := 0; i < n; i++ {
		s = append(s, i)
		caps = append(caps, cap(s))
	}
	return s, caps
}

func checkLocal[T any](t *testing.T, pointers bool) {
	e := elemOf[T](pointers)
	for n := 1; n <= 70; n++ {
		allowed := reachable(e, n)
		if c := localCap[T](n); !allowed[c] {
			t.Errorf("%s: %d appends to a local slice: cap %d, model %v",
				e.name, n, c, allowed)
		}
		if c := cap(returned[T](n)); !allowed[c] {
			t.Errorf("%s: %d appends to a returned slice: cap %d, model %v",
				e.name, n, c, allowed)
		}
		s, caps := observed[T](n)
		checkSteps(t, e, caps)
		if c := cap(s); !allowed[c] {
			t.Errorf("%s: %d appends, capacity observed: cap %d, model %v",
				e.name, n, c, allowed)
		}
	}
}

// checkSteps checks that each capacity of caps (after each append of one
// value to a nil slice) is one the model allows.
func checkSteps(t *testing.T, e elem, caps []int) {
	prev := 0
	for i, c := range caps {
		ok := false
		for _, d := range next(e, i, prev) {
			ok = ok || c == d
		}
		if !ok {
			t.Errorf("%s: append %d to cap %d: cap %d, model %v",
				e.name, i+1, prev, c, next(e, i, prev))
		}
		prev = c
	}
}

// TestLocalCapGC checks that the capacities of slices gc may allocate on
// the stack are among those the model allows.
func TestLocalCapGC(t *testing.T) {
	checkLocal[byte](t, false)
	checkLocal[int16](t, false)
	checkLocal[[3]byte](t, false)
	checkLocal[int64](t, false)
	checkLocal[[12]byte](t, false)
	checkLocal[[5]int32](t, false)
	checkLocal[[32]byte](t, false)
	checkLocal[[40]byte](t, false)
	checkLocal[*int](t, true)
	checkLocal[string](t, true)
	checkLocal[[]int](t, true)
	ints := elemOf[int](false)
	bytes := elemOf[byte](false)
	for n := 1; n <= 70; n++ {
		if c := localCapInt(n); !reachable(ints, n)[c] {
			t.Errorf("int: %d appends to a local slice: cap %d", n, c)
		}
		if c := cap(returnedBytes(n)); !reachable(bytes, n)[c] {
			t.Errorf("byte: %d appends to a returned slice: cap %d", n, c)
		}
		_, caps := observedInt(n)
		checkSteps(t, ints, caps)
	}
}

// nextSliceCapLoop is runtime.nextslicecap as written there.
func nextSliceCapLoop(newLen, oldCap int) int {
	newcap := oldCap
	doublecap := newcap + newcap
	if newLen > doublecap {
		return newLen
	}
	const threshold = 256
	if oldCap < threshold {
		return doublecap
	}
	for {
		newcap += (newcap + 3*threshold) >> 2
		if uint(newcap) >= uint(newLen) {
			break
		}
	}
	if newcap <= 0 {
		return newLen
	}
	return newcap
}

// TestNextSliceCap checks the unrolled loop of nextSliceCap.
func TestNextSliceCap(t *testing.T) {
	for oldCap := 0; oldCap < 5000; oldCap++ {
		for newLen := oldCap + 1; newLen <= 2*oldCap+1; newLen++ {
			if got, want := nextSliceCap(newLen, oldCap),
				nextSliceCapLoop(newLen, oldCap); got != want {
				t.Fatalf("nextSliceCap(%d, %d) = %d, want %d",
					newLen, oldCap, got, want)
			}
		}
	}
	const maxInt = int(^uint(0) >> 1)
	for _, oldCap := range []int{maxInt / 2, maxInt/2 + 1, maxInt - 1} {
		for _, newLen := range []int{oldCap + 1, maxInt} {
			if got, want := nextSliceCap(newLen, oldCap),
				nextSliceCapLoop(newLen, oldCap); got != want {
				t.Fatalf("nextSliceCap(%d, %d) = %d, want %d",
					newLen, oldCap, got, want)
			}
		}
	}
}
