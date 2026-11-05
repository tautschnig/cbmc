// Package rt is the runtime support of CBMC's Go front-end: operations on
// strings that need loops, written in the subset of Go the front-end
// converts. cbmc-go-helper type-checks it as package "cprover/rt" next to
// the program, and the front-end converts it with the program and calls
// these functions where the specification requires (src/go/go_convert.cpp).
//
// Each function cites the section of the Go specification (go1.27) that
// defines the operation. Loops run once per byte or rune, so verifying a
// program that uses them needs a loop bound at least the string length.
package rt

// StringEqual reports whether a and b are equal (Go1.27
// §Comparison_operators: "Two string values are compared lexically
// byte-wise.").
func StringEqual(a, b string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := 0; i < len(a); i++ {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

// StringLess reports whether a < b (Go1.27 §Comparison_operators: "String
// types are comparable and ordered. Two string values are compared
// lexically byte-wise.").
func StringLess(a, b string) bool {
	n := min(len(a), len(b))
	for i := 0; i < n; i++ {
		if a[i] != b[i] {
			return a[i] < b[i]
		}
	}
	return len(a) < len(b)
}

const (
	runeError = 0xFFFD   // the Unicode replacement character
	maxRune   = 0x10FFFF // the largest Unicode code point
)

// DecodeRune decodes the UTF-8 encoded code point that starts at byte index
// i of s, returning it and its length in bytes. Go1.27 §For_range: "If the
// iteration encounters an invalid UTF-8 sequence, the second value will be
// 0xFFFD, the Unicode replacement character, and the next iteration will
// advance a single byte in the string." Valid sequences are those of the
// Unicode standard: shortest form, at most 0x10FFFF, and no surrogates.
func DecodeRune(s string, i int) (rune, int) {
	b0 := s[i]
	if b0 < 0x80 {
		return rune(b0), 1
	}
	var n int
	var r rune
	var lo byte = 0x80 // bounds of the second byte
	var hi byte = 0xBF
	switch {
	case b0 >= 0xC2 && b0 <= 0xDF:
		n, r = 2, rune(b0&0x1F)
	case b0 >= 0xE0 && b0 <= 0xEF:
		n, r = 3, rune(b0&0x0F)
		if b0 == 0xE0 {
			lo = 0xA0 // not overlong
		} else if b0 == 0xED {
			hi = 0x9F // not a surrogate
		}
	case b0 >= 0xF0 && b0 <= 0xF4:
		n, r = 4, rune(b0&0x07)
		if b0 == 0xF0 {
			lo = 0x90 // not overlong
		} else if b0 == 0xF4 {
			hi = 0x8F // at most 0x10FFFF
		}
	default:
		return runeError, 1
	}
	if i+n > len(s) {
		return runeError, 1
	}
	for k := 1; k < n; k++ {
		b := s[i+k]
		if (k == 1 && (b < lo || b > hi)) || (k > 1 && (b < 0x80 || b > 0xBF)) {
			return runeError, 1
		}
		r = r<<6 | rune(b&0x3F)
	}
	return r, n
}

// EncodeRune returns the UTF-8 encoding of r. Go1.27
// §Conversions_to_and_from_a_string_type: "an integer value may be converted
// to a string type. This form of conversion yields a string containing the
// (possibly multi-byte) UTF-8 representation of the Unicode code point with
// the given integer value. Values outside the range of valid Unicode code
// points are converted to "\uFFFD"." The front-end passes -1 for integers
// that do not fit in a rune. Surrogates are not valid code points either.
func EncodeRune(r rune) string {
	if r < 0 || r > maxRune || (r >= 0xD800 && r <= 0xDFFF) {
		r = runeError
	}
	switch {
	case r < 0x80:
		return string([]byte{byte(r)})
	case r < 0x800:
		return string([]byte{0xC0 | byte(r>>6), 0x80 | byte(r)&0x3F})
	case r < 0x10000:
		return string([]byte{0xE0 | byte(r>>12), 0x80 | byte(r>>6)&0x3F,
			0x80 | byte(r)&0x3F})
	default:
		return string([]byte{0xF0 | byte(r>>18), 0x80 | byte(r>>12)&0x3F,
			0x80 | byte(r>>6)&0x3F, 0x80 | byte(r)&0x3F})
	}
}

// StringToRunes converts s to its code points (Go1.27
// §Conversions_to_and_from_a_string_type).
func StringToRunes(s string) []rune {
	runes := []rune{}
	for i := 0; i < len(s); {
		r, n := DecodeRune(s, i)
		runes = append(runes, r)
		i += n
	}
	return runes
}

// RunesToString converts code points to a string: "Converting a slice of
// runes to a string type yields a string that is the concatenation of the
// individual rune values converted to strings."
func RunesToString(runes []rune) string {
	s := ""
	for _, r := range runes {
		s += EncodeRune(r)
	}
	return s
}

// Floating-point helpers for ComplexDiv.

func isNaN(f float64) bool { return f != f }

func isInf(f float64) bool { return f > maxFloat64 || f < -maxFloat64 }

func isFinite(f float64) bool { return !isNaN(f) && !isInf(f) }

const maxFloat64 = 0x1p1023 * (1 + (1 - 0x1p-52))

// posInf returns +Inf. (The runtime package has no package-level variables:
// only the program's are initialised.)
func posInf() float64 {
	zero := 0.0
	return 1 / zero
}

// signBit reports whether f is negative, -0 included (NaN signs do not
// matter here).
func signBit(f float64) bool { return f < 0 || (f == 0 && 1/f < 0) }

// copySign returns x with the sign of y.
func copySign(x, y float64) float64 {
	if signBit(x) != signBit(y) {
		return -x
	}
	return x
}

func abs(f float64) float64 {
	if signBit(f) {
		return -f
	}
	return f
}

// infToOne returns a signed 1 for an infinity and a signed 0 otherwise,
// with the sign of f.
func infToOne(f float64) float64 {
	g := 0.0
	if isInf(f) {
		g = 1.0
	}
	return copySign(g, f)
}

// ComplexDiv returns n/m. Go1.27 §Arithmetic_operators: "The result of a
// floating-point or complex division by zero is not specified beyond the
// IEEE 754 standard". The specification does not fix the algorithm; this
// one gives the results of gc, the reference implementation, which divides
// complex64 values as complex128 values: Smith's algorithm (Robert L.
// Smith, Algorithm 116: Complex division, Commun. ACM 5(8):435, 1962),
// with results that are NaN in both parts corrected to infinities and
// zeros as in C99 (ISO/IEC 9899:1999 G.5.1).
func ComplexDiv(n, m complex128) complex128 {
	a, b := real(n), imag(n)
	c, d := real(m), imag(m)
	var e, f float64
	if abs(c) >= abs(d) {
		ratio := d / c
		denom := c + ratio*d
		e = (a + b*ratio) / denom
		f = (b - a*ratio) / denom
	} else {
		ratio := c / d
		denom := d + ratio*c
		e = (a*ratio + b) / denom
		f = (b*ratio - a) / denom
	}
	if isNaN(e) && isNaN(f) {
		switch {
		case m == 0 && (!isNaN(a) || !isNaN(b)):
			e = copySign(posInf(), c) * a
			f = copySign(posInf(), c) * b
		case (isInf(a) || isInf(b)) && isFinite(c) && isFinite(d):
			a = infToOne(a)
			b = infToOne(b)
			e = posInf() * (a*c + b*d)
			f = posInf() * (b*c - a*d)
		case (isInf(c) || isInf(d)) && isFinite(a) && isFinite(b):
			c = infToOne(c)
			d = infToOne(d)
			e = 0 * (a*c + b*d)
			f = 0 * (b*c - a*d)
		}
	}
	return complex(e, f)
}

// Error is the type of the values of run-time panics (Go1.27
// §Run_time_panics: "Execution errors such as attempting to index an array
// out of bounds trigger a run-time panic equivalent to a call of the
// built-in function panic with a value of the implementation-defined
// interface type runtime.Error. That type satisfies the predeclared
// interface type error."). The message is gc's.
type Error struct{ msg string }

// Error returns the message of the run-time error.
func (e Error) Error() string { return e.msg }

// RuntimeError distinguishes run-time errors, as in package runtime.
func (e Error) RuntimeError() {}

// Intrinsics, whose bodies the front-end generates (go_convertert::
// intrinsic_body): they are not expressible in the Go it converts.

// Comparable reports whether the dynamic type of x is comparable (Go1.27
// §Comparison_operators), or x is nil; the standard library uses
// reflection for this.
func Comparable(x any) bool

// SliceLen returns the length of the slice x (of any slice type), as
// reflectlite.ValueOf(x).Len(), which panics if x is not a slice.
func SliceLen(x any) int

// SliceSwap swaps the elements i and j of the slice x (of any slice
// type), as the function that reflectlite.Swapper(x) returns, which
// panics if x is not a slice or i or j is out of range.
func SliceSwap(x any, i, j int)

// SameType reports whether x and y have the same dynamic type (both are
// nil, or neither is and their types are identical, Go1.27
// §Type_identity); package sync/atomic uses unsafe for this.
func SameType(x, y any) bool

// AsTarget checks target for errors.As: 0 if it is a non-nil pointer to
// an interface type or to a type that implements error, 1 if it is not a
// non-nil pointer, 2 otherwise (package errors uses reflection for this).
func AsTarget(target any) int

// AsAssign sets *target to err if err's dynamic type is assignable to the
// element type of target's pointer type (Go1.27 §Assignability), and
// reports whether it did (errors.As; target is valid, see AsTarget).
func AsAssign(target any, err error) bool

// Float64bits returns the IEEE 754 binary representation of f, as
// math.Float64bits.
func Float64bits(f float64) uint64

// Float64frombits returns the float64 with representation b.
func Float64frombits(b uint64) float64

// Float32bits returns the IEEE 754 binary representation of f.
func Float32bits(f float32) uint32

// Float32frombits returns the float32 with representation b.
func Float32frombits(b uint32) float32

// Operations of the models of sync and sync/atomic, whose calls (as
// statements) the front-end converts (go_convertert::
// convert_intrinsic_call); the bodies are not used.

// AtomicBegin starts an atomic section: no other goroutine runs until the
// matching AtomicEnd. A section must not panic.
func AtomicBegin() {}

// AtomicEnd ends the atomic section that AtomicBegin started.
func AtomicEnd() {}

// RaceRead and RaceWrite are an access (a read or a write) of the variable
// that p points to for the data-race check (--go-race-check), as the race
// package's Read and Write annotate one under gc's -race; otherwise they
// do nothing.
func RaceRead(p any)  {}
func RaceWrite(p any) {}

// AtomicBeginWhen starts an atomic section once cond holds, as
// AtomicBegin: the goroutine blocks until then (only the executions in
// which cond holds continue), and if all goroutines block forever, the
// deadlock is reported.
func AtomicBeginWhen(cond bool) {}

// Fatal reports a fatal error with constant message msg and stops the
// program, as the runtime's fatal errors (which deferred functions cannot
// recover from) do: a failing property of class "fatal".
func Fatal(msg string) { panic(msg) }

// Capacities of append. Go1.27 §Appending_and_copying_slices: "If the
// capacity of s is not large enough to fit the additional values, append
// allocates a new, sufficiently large underlying array". The spec leaves
// the capacity open; the functions below give the capacities gc go1.27
// chooses (runtime/slice.go, runtime/msize.go and the stack allocation of
// backing stores in cmd/compile), which rt_test.go checks against the gc
// that builds it. They have no loops, so that they need no unwinding.

// GrowCapGC returns the capacity runtime.growslice gives a slice of
// elements of size bytes (with pointers or not) that has capacity oldCap
// and must grow to length newLen.
func GrowCapGC(newLen, oldCap, size int, pointers bool) int {
	if size == 0 {
		return newLen
	}
	newcap := nextSliceCap(newLen, oldCap)
	return int(roundUpSize(uintptr(newcap)*uintptr(size), !pointers) / uintptr(size))
}

// StackCapGC returns the capacity of the stack buffer that gc may give
// the backing store of a slice of elements of size bytes that does not
// escape (cmd/compile/internal/ssagen getBackingStoreInfo: 32 bytes, the
// default of -d=variablemakethreshold), or 0 if there is none. gc uses it
// for the first append to an empty slice that fits.
func StackCapGC(size int) int {
	if size == 0 || size > stackBufferSize {
		return 0
	}
	return stackBufferSize / size
}

// RoundUpCapGC returns the capacity runtime.growsliceBuf gives a slice of
// length newLen that fits in its stack buffer (StackCapGC), which is also
// the capacity runtime.moveSliceNoCap gives such a slice when moving it to
// the heap.
func RoundUpCapGC(newLen, size int, pointers bool) int {
	return int(roundUpSize(uintptr(newLen)*uintptr(size), !pointers) / uintptr(size))
}

const stackBufferSize = 32

// GrowCapTinyGo returns the capacity TinyGo's runtime.sliceGrow (TinyGo
// 0.44, src/runtime/slice.go) gives a slice that must grow to length
// newLen: the smallest power of two above newLen (1 << bits.Len(newLen)),
// whatever the element type. rt_tinygo_test.go checks it against the
// TinyGo that runs it.
func GrowCapTinyGo(newLen int) int {
	return 1 << bitLen(uint(newLen))
}

// bitLen is math/bits.Len: the number of bits needed to represent x, by a
// binary search without loops (x>>32 is 0 for 32-bit uint).
func bitLen(x uint) int {
	n := 0
	if x>>32 != 0 {
		x >>= 32
		n += 32
	}
	if x>>16 != 0 {
		x >>= 16
		n += 16
	}
	if x>>8 != 0 {
		x >>= 8
		n += 8
	}
	if x>>4 != 0 {
		x >>= 4
		n += 4
	}
	if x>>2 != 0 {
		x >>= 2
		n += 2
	}
	if x>>1 != 0 {
		x >>= 1
		n++
	}
	return n + int(x)
}

// GopherJS (which compiles Go to JavaScript) is not modelled: its
// $calculateNewCapacity (compiler/prelude/prelude.js) gives
// max(newLen, oldCap < 1024 ? 2*oldCap : floor(oldCap*5/4)), without
// rounding to size classes, so that --go-compiler any covers it.

// nextSliceCap is runtime.nextslicecap, with its loop unrolled: newLen is
// at most twice oldCap there, and each step grows newcap by more than a
// quarter, so that four steps (1.25^4 > 2) reach newLen.
func nextSliceCap(newLen, oldCap int) int {
	newcap := oldCap
	doublecap := newcap + newcap
	if newLen > doublecap {
		return newLen
	}
	const threshold = 256
	if oldCap < threshold {
		return doublecap
	}
	newcap += (newcap + 3*threshold) >> 2
	if uint(newcap) < uint(newLen) {
		newcap += (newcap + 3*threshold) >> 2
		if uint(newcap) < uint(newLen) {
			newcap += (newcap + 3*threshold) >> 2
			if uint(newcap) < uint(newLen) {
				newcap += (newcap + 3*threshold) >> 2
			}
		}
	}
	if newcap <= 0 {
		return newLen
	}
	return newcap
}

const (
	ptrSize                = 4 << (^uintptr(0) >> 63)
	maxSmallSize           = 32768
	mallocHeaderSize       = 8
	minSizeForMallocHeader = ptrSize * 8 * ptrSize
	pageSize               = 8192
)

// roundUpSize is runtime.roundupsize: the size of the memory block that
// mallocgc allocates for size bytes, less its malloc header if any.
func roundUpSize(size uintptr, noscan bool) uintptr {
	reqSize := size
	if reqSize <= maxSmallSize-mallocHeaderSize {
		if !noscan && reqSize > minSizeForMallocHeader {
			reqSize += mallocHeaderSize
		}
		return sizeClass(reqSize) - (reqSize - size)
	}
	reqSize += pageSize - 1
	if reqSize < size {
		return size
	}
	return reqSize &^ (pageSize - 1)
}

// sizeClass returns the smallest of gc's size classes
// (internal/runtime/gc/sizeclasses.go) of at least size bytes.
func sizeClass(size uintptr) uintptr {
	switch {
	case size == 0:
		return 0
	case size <= 8:
		return 8
	case size <= 16:
		return 16
	case size <= 24:
		return 24
	case size <= 32:
		return 32
	case size <= 48:
		return 48
	case size <= 64:
		return 64
	case size <= 80:
		return 80
	case size <= 96:
		return 96
	case size <= 112:
		return 112
	case size <= 128:
		return 128
	case size <= 144:
		return 144
	case size <= 160:
		return 160
	case size <= 176:
		return 176
	case size <= 192:
		return 192
	case size <= 208:
		return 208
	case size <= 224:
		return 224
	case size <= 240:
		return 240
	case size <= 256:
		return 256
	case size <= 288:
		return 288
	case size <= 320:
		return 320
	case size <= 352:
		return 352
	case size <= 384:
		return 384
	case size <= 416:
		return 416
	case size <= 448:
		return 448
	case size <= 480:
		return 480
	case size <= 512:
		return 512
	case size <= 576:
		return 576
	case size <= 640:
		return 640
	case size <= 704:
		return 704
	case size <= 768:
		return 768
	case size <= 896:
		return 896
	case size <= 1024:
		return 1024
	case size <= 1152:
		return 1152
	case size <= 1280:
		return 1280
	case size <= 1408:
		return 1408
	case size <= 1536:
		return 1536
	case size <= 1792:
		return 1792
	case size <= 2048:
		return 2048
	case size <= 2304:
		return 2304
	case size <= 2688:
		return 2688
	case size <= 3072:
		return 3072
	case size <= 3200:
		return 3200
	case size <= 3456:
		return 3456
	case size <= 4096:
		return 4096
	case size <= 4864:
		return 4864
	case size <= 5376:
		return 5376
	case size <= 6144:
		return 6144
	case size <= 6528:
		return 6528
	case size <= 6784:
		return 6784
	case size <= 6912:
		return 6912
	case size <= 8192:
		return 8192
	case size <= 9472:
		return 9472
	case size <= 9728:
		return 9728
	case size <= 10240:
		return 10240
	case size <= 10880:
		return 10880
	case size <= 12288:
		return 12288
	case size <= 13568:
		return 13568
	case size <= 14336:
		return 14336
	case size <= 16384:
		return 16384
	case size <= 18432:
		return 18432
	case size <= 19072:
		return 19072
	case size <= 20480:
		return 20480
	case size <= 21760:
		return 21760
	case size <= 24576:
		return 24576
	case size <= 27264:
		return 27264
	case size <= 28672:
		return 28672
	case size <= 32768:
		return 32768
	}
	return size
}

// Unsupported reports, with constant description what, an operation that
// a model does not support: a failing property (of class "unsupported",
// as for calls of functions without a model), after which the path stops.
func Unsupported(what string) { panic(what) }
