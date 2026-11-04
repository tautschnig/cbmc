package reflect

import "unsafe"

// Intrinsics: the front-end generates their bodies (src/go/go_reflect.cpp),
// which test the type id t against each type the model describes. A
// pointer p (or m, s, dst, src) points to a value of the type with id t.

// typeOf returns the descriptor of the type with id t, nil for 0.
func typeOf(t int32) *rtype

// ifaceType returns the id of the dynamic type of i (0 for nil).
func ifaceType(i any) int32

// ifaceData returns the pointer to the dynamic value of i.
func ifaceData(i any) unsafe.Pointer

// makeIface returns the interface value of dynamic type t whose dynamic
// value is at p (which must not change afterwards).
func makeIface(t int32, p unsafe.Pointer) any

// box returns an interface value holding a copy of the value at p (not of
// an interface type).
func box(t int32, p unsafe.Pointer) any

// assign copies the value at src to dst.
func assign(t int32, dst, src unsafe.Pointer)

// alloc returns a new variable of type t, with its zero value.
func alloc(t int32) unsafe.Pointer

// equal reports whether the values at x and y are equal (Go1.27
// §Comparison_operators), for comparable types.
func equal(t int32, x, y unsafe.Pointer) bool

// mapFind returns the index of the entry of map *m with key *k, or -1.
func mapFind(t int32, m, k unsafe.Pointer) int

// mapStore sets the entry of map *m with key *k to *v.
func mapStore(t int32, m, k, v unsafe.Pointer)

// mapDelete deletes the entry of map *m with key *k.
func mapDelete(t int32, m, k unsafe.Pointer)

// makeMap sets *m to a new, empty map.
func makeMap(t int32, m unsafe.Pointer)

// mapHeader returns the arrays of the keys and the values of map *m, and
// its number of entries.
func mapHeader(t int32, m unsafe.Pointer) (keys, vals unsafe.Pointer, n int)

// makeSlice sets *s to a new slice of length n and capacity c.
func makeSlice(t int32, s unsafe.Pointer, n, c int)

// sliceHeader returns the pointer to the first element, the length and
// the capacity of slice *s.
func sliceHeader(t int32, s unsafe.Pointer) (data unsafe.Pointer, n, c int)

// setSlice sets *s to the slice of n elements from data, of capacity c.
func setSlice(t int32, s unsafe.Pointer, data unsafe.Pointer, n, c int)

// chanLenCap returns the number of buffered elements and the capacity of
// channel *c.
func chanLenCap(t int32, c unsafe.Pointer) (n, capacity int)

// pointerAt returns the pointer that the value at p is (of a pointer,
// map, channel or unsafe.Pointer type; the code of a function value).
func pointerAt(t int32, p unsafe.Pointer) unsafe.Pointer

// setPointer sets the pointer at p (of a pointer type) to q.
func setPointer(t int32, p unsafe.Pointer, q unsafe.Pointer)
