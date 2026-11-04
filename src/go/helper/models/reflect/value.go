package reflect

import (
	"cprover/rt"
	"iter"
	"unsafe"
)

type flag uintptr

const (
	// flagAddr: the value is addressable (Go1.27 §Address_operators; for
	// reflect, obtained through a pointer).
	flagAddr flag = 1 << iota
	// flagRO: obtained through an unexported field.
	flagRO
	// flagMethod: a method value (see Value.Method); ptr points to (a copy
	// of) the receiver, of type mrecv, and the method is mrecv.methods[mindex].
	flagMethod
)

// Value is the reflection interface to a Go value: the descriptor of its
// type (nil for the zero Value), a pointer to the value, and flags. Values
// that are not addressable point to variables that are not changed.
type Value struct {
	typ_   *rtype
	ptr    unsafe.Pointer
	flag   flag
	mrecv  *rtype
	mindex int
}

func (v Value) kind() Kind {
	if v.typ_ == nil {
		return Invalid
	}
	return v.typ_.kind
}

func (v Value) mustBe(k Kind, method string) {
	if v.kind() != k {
		panic(&ValueError{method, v.kind()})
	}
}

func (v Value) mustBeExported(method string) {
	if v.typ_ == nil {
		panic(&ValueError{method, Invalid})
	}
	if v.flag&flagRO != 0 {
		panic("reflect: " + method + " using value obtained using unexported field")
	}
}

func (v Value) mustBeAssignable(method string) {
	if v.typ_ == nil {
		panic(&ValueError{method, Invalid})
	}
	if v.flag&flagRO != 0 {
		panic("reflect: " + method + " using value obtained using unexported field")
	}
	if v.flag&flagAddr == 0 {
		panic("reflect: " + method + " using unaddressable value")
	}
}

// at returns the value of type t at p, with flags fl.
func at(t *rtype, p unsafe.Pointer, fl flag) Value {
	return Value{typ_: t, ptr: p, flag: fl}
}

// copyOf returns a (non-addressable) copy of the value of type t at p.
func copyOf(t *rtype, p unsafe.Pointer, fl flag) Value {
	cell := alloc(t.tid)
	assign(t.tid, cell, p)
	return Value{typ_: t, ptr: cell, flag: fl &^ flagAddr}
}

// element returns the pointer to element i of the array at p, of
// elements of type t.
func element(p unsafe.Pointer, t *rtype, i int) unsafe.Pointer {
	return unsafe.Add(p, uintptr(i)*t.stride)
}

// byteType returns the descriptor of byte.
func byteType() *rtype { return typeOf(ifaceType(any(byte(0)))) }

// ValueOf returns a new Value initialized to the concrete value stored in
// the interface i. ValueOf(nil) returns the zero Value.
func ValueOf(i any) Value {
	if i == nil {
		return Value{}
	}
	return Value{typ_: typeOf(ifaceType(i)), ptr: ifaceData(i), flag: 0}
}

// Indirect returns the value that v points to.
func Indirect(v Value) Value {
	if v.Kind() != Pointer {
		return v
	}
	return v.Elem()
}

// Zero returns a Value representing the zero value for the specified
// type.
func Zero(typ Type) Value {
	if typ == nil {
		panic("reflect: Zero(nil)")
	}
	t := typ.common()
	return Value{typ_: t, ptr: alloc(t.tid), flag: 0}
}

// New returns a Value representing a pointer to a new zero value for the
// specified type.
func New(typ Type) Value {
	if typ == nil {
		panic("reflect: New(nil)")
	}
	t := typ.common()
	p := t.ptrTo
	if p == nil {
		rt.Unsupported("reflect.New of a type whose pointer type the model does not describe")
	}
	cell := alloc(p.tid)
	setPointer(p.tid, cell, alloc(t.tid))
	return Value{typ_: p, ptr: cell, flag: 0}
}

// NewAt returns a Value representing a pointer to a value of the
// specified type, using p as that pointer.
func NewAt(typ Type, p unsafe.Pointer) Value {
	t := typ.common()
	pt := t.ptrTo
	if pt == nil {
		rt.Unsupported("reflect.NewAt of a type whose pointer type the model does not describe")
	}
	cell := alloc(pt.tid)
	setPointer(pt.tid, cell, p)
	return Value{typ_: pt, ptr: cell, flag: 0}
}

// MakeSlice creates a new zero-initialized slice value for the specified
// slice type, length, and capacity.
func MakeSlice(typ Type, len, cap int) Value {
	if typ.Kind() != Slice {
		panic("reflect.MakeSlice of non-slice type")
	}
	if len < 0 {
		panic("reflect.MakeSlice: negative len")
	}
	if cap < 0 {
		panic("reflect.MakeSlice: negative cap")
	}
	if len > cap {
		panic("reflect.MakeSlice: len > cap")
	}
	t := typ.common()
	cell := alloc(t.tid)
	makeSlice(t.tid, cell, len, cap)
	return Value{typ_: t, ptr: cell, flag: 0}
}

// SliceAt returns a Value representing a slice whose underlying data
// starts at p, with length and capacity equal to n.
func SliceAt(typ Type, p unsafe.Pointer, n int) Value {
	rt.Unsupported("reflect.SliceAt")
	return Value{}
}

// MakeMap creates a new map with the specified type.
func MakeMap(typ Type) Value {
	return MakeMapWithSize(typ, 0)
}

// MakeMapWithSize creates a new map with the specified type and initial
// space for approximately n elements.
func MakeMapWithSize(typ Type, n int) Value {
	if typ.Kind() != Map {
		panic("reflect.MakeMapWithSize of non-map type")
	}
	t := typ.common()
	cell := alloc(t.tid)
	makeMap(t.tid, cell)
	return Value{typ_: t, ptr: cell, flag: 0}
}

// MakeChan creates a new channel with the specified type and buffer size
// (not modelled).
func MakeChan(typ Type, buffer int) Value {
	rt.Unsupported("reflect.MakeChan")
	return Value{}
}

// MakeFunc returns a new function of the given Type that wraps the
// function fn (not modelled).
func MakeFunc(typ Type, fn func(args []Value) (results []Value)) Value {
	rt.Unsupported("reflect.MakeFunc")
	return Value{}
}

// Swapper returns a function that swaps the elements in the provided
// slice.
func Swapper(slice any) func(i, j int) {
	v := ValueOf(slice)
	if v.Kind() != Slice {
		panic(&ValueError{Method: "Swapper", Kind: v.Kind()})
	}
	return func(i, j int) { rt.SliceSwap(slice, i, j) }
}

// TypeAssert is semantically equivalent to (but more efficient than)
// v2, ok := v.Interface().(T).
func TypeAssert[T any](v Value) (T, bool) {
	x, ok := valueInterface(v, true).(T)
	return x, ok
}

// IsValid reports whether v represents a value.
func (v Value) IsValid() bool { return v.typ_ != nil }

// Kind returns v's Kind. If v is the zero Value, Kind returns Invalid.
func (v Value) Kind() Kind { return v.kind() }

// Type returns v's type.
func (v Value) Type() Type {
	if v.typ_ == nil {
		panic(&ValueError{"reflect.Value.Type", Invalid})
	}
	return v.typ_
}

// CanAddr reports whether the value's address can be obtained with Addr.
func (v Value) CanAddr() bool { return v.flag&flagAddr != 0 }

// CanSet reports whether the value of v can be changed.
func (v Value) CanSet() bool { return v.flag&(flagAddr|flagRO) == flagAddr }

// CanInterface reports whether Interface can be used without panicking.
func (v Value) CanInterface() bool {
	if v.typ_ == nil {
		panic(&ValueError{"reflect.Value.CanInterface", Invalid})
	}
	return v.flag&flagRO == 0
}

// Interface returns v's current value as an interface{}.
func (v Value) Interface() (i any) {
	return valueInterface(v, true)
}

func valueInterface(v Value, safe bool) any {
	if v.typ_ == nil {
		panic(&ValueError{"reflect.Value.Interface", Invalid})
	}
	if safe && v.flag&flagRO != 0 {
		panic("reflect.Value.Interface: cannot return value obtained from unexported field or method")
	}
	if v.flag&flagMethod != 0 {
		rt.Unsupported("reflect.Value.Interface of a method value")
	}
	if v.typ_.kind == Interface {
		return *(*any)(v.ptr)
	}
	if v.flag&flagAddr == 0 {
		return makeIface(v.typ_.tid, v.ptr)
	}
	return box(v.typ_.tid, v.ptr)
}

// InterfaceData is not modelled.
func (v Value) InterfaceData() [2]uintptr {
	rt.Unsupported("reflect.Value.InterfaceData")
	return [2]uintptr{}
}

// Addr returns a pointer value representing the address of v.
func (v Value) Addr() Value {
	if v.flag&flagAddr == 0 {
		panic("reflect.Value.Addr of unaddressable value")
	}
	p := v.typ_.ptrTo
	if p == nil {
		rt.Unsupported("reflect.Value.Addr of a value whose pointer type the model does not describe")
	}
	cell := alloc(p.tid)
	setPointer(p.tid, cell, v.ptr)
	return Value{typ_: p, ptr: cell, flag: v.flag & flagRO}
}

// UnsafeAddr returns a pointer to v's data, as a uintptr.
func (v Value) UnsafeAddr() uintptr {
	if v.typ_ == nil {
		panic(&ValueError{"reflect.Value.UnsafeAddr", Invalid})
	}
	if v.flag&flagAddr == 0 {
		panic("reflect.Value.UnsafeAddr of unaddressable value")
	}
	return uintptr(v.ptr)
}

// Elem returns the value that the interface v contains or that the
// pointer v points to.
func (v Value) Elem() Value {
	switch v.kind() {
	case Interface:
		x := ValueOf(*(*any)(v.ptr))
		if x.typ_ != nil {
			x.flag |= v.flag & flagRO
		}
		return x
	case Pointer:
		p := pointerAt(v.typ_.tid, v.ptr)
		if p == nil {
			return Value{}
		}
		if v.typ_.elem == nil {
			rt.Unsupported("reflect.Value.Elem of a pointer whose element type the model does not describe")
		}
		return Value{typ_: v.typ_.elem, ptr: p, flag: v.flag&flagRO | flagAddr}
	}
	panic(&ValueError{"reflect.Value.Elem", v.kind()})
}

// Bool returns v's underlying value.
func (v Value) Bool() bool {
	v.mustBe(Bool, "reflect.Value.Bool")
	return *(*bool)(v.ptr)
}

// CanInt reports whether Int can be used without panicking.
func (v Value) CanInt() bool {
	switch v.kind() {
	case Int, Int8, Int16, Int32, Int64:
		return true
	}
	return false
}

// Int returns v's underlying value.
func (v Value) Int() int64 {
	p := v.ptr
	switch v.kind() {
	case Int:
		return int64(*(*int)(p))
	case Int8:
		return int64(*(*int8)(p))
	case Int16:
		return int64(*(*int16)(p))
	case Int32:
		return int64(*(*int32)(p))
	case Int64:
		return *(*int64)(p)
	}
	panic(&ValueError{"reflect.Value.Int", v.kind()})
}

// CanUint reports whether Uint can be used without panicking.
func (v Value) CanUint() bool {
	switch v.kind() {
	case Uint, Uint8, Uint16, Uint32, Uint64, Uintptr:
		return true
	}
	return false
}

// Uint returns v's underlying value.
func (v Value) Uint() uint64 {
	p := v.ptr
	switch v.kind() {
	case Uint:
		return uint64(*(*uint)(p))
	case Uint8:
		return uint64(*(*uint8)(p))
	case Uint16:
		return uint64(*(*uint16)(p))
	case Uint32:
		return uint64(*(*uint32)(p))
	case Uint64:
		return *(*uint64)(p)
	case Uintptr:
		return uint64(*(*uintptr)(p))
	}
	panic(&ValueError{"reflect.Value.Uint", v.kind()})
}

// CanFloat reports whether Float can be used without panicking.
func (v Value) CanFloat() bool {
	k := v.kind()
	return k == Float32 || k == Float64
}

// Float returns v's underlying value, as a float64.
func (v Value) Float() float64 {
	switch v.kind() {
	case Float32:
		return float64(*(*float32)(v.ptr))
	case Float64:
		return *(*float64)(v.ptr)
	}
	panic(&ValueError{"reflect.Value.Float", v.kind()})
}

// CanComplex reports whether Complex can be used without panicking.
func (v Value) CanComplex() bool {
	k := v.kind()
	return k == Complex64 || k == Complex128
}

// Complex returns v's underlying value, as a complex128.
func (v Value) Complex() complex128 {
	switch v.kind() {
	case Complex64:
		return complex128(*(*complex64)(v.ptr))
	case Complex128:
		return *(*complex128)(v.ptr)
	}
	panic(&ValueError{"reflect.Value.Complex", v.kind()})
}

// String returns the string v's underlying value, as a string; for other
// kinds "<T Value>".
func (v Value) String() string {
	switch v.kind() {
	case Invalid:
		return "<invalid Value>"
	case String:
		return *(*string)(v.ptr)
	}
	return "<" + v.typ_.str + " Value>"
}

// Bytes returns v's underlying value (a slice of bytes, or an addressable
// array of bytes).
func (v Value) Bytes() []byte {
	switch v.kind() {
	case Slice:
		if v.typ_.elem.kind != Uint8 {
			panic("reflect.Value.Bytes of non-byte slice")
		}
		data, n, c := sliceHeader(v.typ_.tid, v.ptr)
		if data == nil {
			return nil
		}
		return unsafe.Slice((*byte)(data), c)[:n:c]
	case Array:
		if v.typ_.elem.kind != Uint8 {
			panic("reflect.Value.Bytes of non-byte array")
		}
		if v.flag&flagAddr == 0 {
			panic("reflect.Value.Bytes of unaddressable byte array")
		}
		return unsafe.Slice((*byte)(v.ptr), v.typ_.len)
	}
	panic(&ValueError{"reflect.Value.Bytes", v.kind()})
}

// Len returns v's length.
func (v Value) Len() int {
	switch v.kind() {
	case Array:
		return v.typ_.len
	case Chan:
		n, _ := chanLenCap(v.typ_.tid, v.ptr)
		return n
	case Map:
		_, _, n := mapHeader(v.typ_.tid, v.ptr)
		return n
	case Slice:
		_, n, _ := sliceHeader(v.typ_.tid, v.ptr)
		return n
	case String:
		return len(*(*string)(v.ptr))
	case Pointer:
		if v.typ_.elem != nil && v.typ_.elem.kind == Array {
			return v.typ_.elem.len
		}
		panic("reflect: call of reflect.Value.Len on ptr to non-array Value")
	}
	panic(&ValueError{"reflect.Value.Len", v.kind()})
}

// Cap returns v's capacity.
func (v Value) Cap() int {
	switch v.kind() {
	case Array:
		return v.typ_.len
	case Chan:
		_, c := chanLenCap(v.typ_.tid, v.ptr)
		return c
	case Slice:
		_, _, c := sliceHeader(v.typ_.tid, v.ptr)
		return c
	case Pointer:
		if v.typ_.elem != nil && v.typ_.elem.kind == Array {
			return v.typ_.elem.len
		}
		panic("reflect: call of reflect.Value.Cap on ptr to non-array Value")
	}
	panic(&ValueError{"reflect.Value.Cap", v.kind()})
}

// Index returns v's i'th element.
func (v Value) Index(i int) Value {
	switch v.kind() {
	case Array:
		if uint(i) >= uint(v.typ_.len) {
			panic("reflect: array index out of range")
		}
		et := v.typ_.elem
		return Value{typ_: et, ptr: element(v.ptr, et, i), flag: v.flag & (flagAddr | flagRO)}
	case Slice:
		data, n, _ := sliceHeader(v.typ_.tid, v.ptr)
		if uint(i) >= uint(n) {
			panic("reflect: slice index out of range")
		}
		et := v.typ_.elem
		return Value{typ_: et, ptr: element(data, et, i), flag: flagAddr | v.flag&flagRO}
	case String:
		s := *(*string)(v.ptr)
		if uint(i) >= uint(len(s)) {
			panic("reflect: string index out of range")
		}
		t := byteType()
		cell := alloc(t.tid)
		*(*byte)(cell) = s[i]
		return Value{typ_: t, ptr: cell, flag: v.flag & flagRO}
	}
	panic(&ValueError{"reflect.Value.Index", v.kind()})
}

// IsNil reports whether its argument v is nil.
func (v Value) IsNil() bool {
	if v.flag&flagMethod != 0 {
		return false
	}
	switch v.kind() {
	case Chan, Func, Map, Pointer, UnsafePointer:
		return pointerAt(v.typ_.tid, v.ptr) == nil
	case Interface:
		return *(*any)(v.ptr) == nil
	case Slice:
		data, _, _ := sliceHeader(v.typ_.tid, v.ptr)
		return data == nil
	}
	panic(&ValueError{"reflect.Value.IsNil", v.kind()})
}

// IsZero reports whether v is the zero value for its type.
func (v Value) IsZero() bool {
	switch v.kind() {
	case Bool:
		return !v.Bool()
	case Int, Int8, Int16, Int32, Int64:
		return v.Int() == 0
	case Uint, Uint8, Uint16, Uint32, Uint64, Uintptr:
		return v.Uint() == 0
	case Float32, Float64:
		f := v.Float()
		return f == 0 && 1/f > 0 // +0 only
	case Complex64, Complex128:
		c := v.Complex()
		r, i := real(c), imag(c)
		return r == 0 && 1/r > 0 && i == 0 && 1/i > 0
	case Array:
		for i := 0; i < v.typ_.len; i++ {
			if !v.Index(i).IsZero() {
				return false
			}
		}
		return true
	case Chan, Func, Interface, Map, Pointer, Slice, UnsafePointer:
		return v.IsNil()
	case String:
		return v.Len() == 0
	case Struct:
		for i := range v.typ_.fields {
			if !v.Field(i).IsZero() {
				return false
			}
		}
		return true
	}
	panic(&ValueError{"reflect.Value.IsZero", v.Kind()})
}

// SetZero sets v to be the zero value of v's type.
func (v Value) SetZero() {
	v.mustBeAssignable("reflect.Value.SetZero")
	assign(v.typ_.tid, v.ptr, alloc(v.typ_.tid))
}

// NumField returns the number of fields in the struct v.
func (v Value) NumField() int {
	v.mustBe(Struct, "reflect.Value.NumField")
	return len(v.typ_.fields)
}

// Field returns the i'th field of the struct v.
func (v Value) Field(i int) Value {
	v.mustBe(Struct, "reflect.Value.Field")
	if uint(i) >= uint(len(v.typ_.fields)) {
		panic("reflect: Field index out of range")
	}
	f := &v.typ_.fields[i]
	fl := v.flag
	if f.pkgPath != "" {
		fl |= flagRO
	}
	return Value{typ_: f.typ, ptr: unsafe.Add(v.ptr, f.off), flag: fl}
}

// FieldByIndex returns the nested field corresponding to index.
func (v Value) FieldByIndex(index []int) Value {
	if len(index) == 1 {
		return v.Field(index[0])
	}
	v.mustBe(Struct, "reflect.Value.FieldByIndex")
	for i, x := range index {
		if i > 0 {
			if v.Kind() == Pointer && v.typ_.elem.kind == Struct {
				if v.IsNil() {
					panic("reflect: indirection through nil pointer to embedded struct")
				}
				v = v.Elem()
			}
		}
		v = v.Field(x)
	}
	return v
}

// FieldByIndexErr returns the nested field corresponding to index, or an
// error for a nil pointer to an embedded struct on the way.
func (v Value) FieldByIndexErr(index []int) (Value, error) {
	if len(index) == 1 {
		return v.Field(index[0]), nil
	}
	v.mustBe(Struct, "reflect.Value.FieldByIndexErr")
	for i, x := range index {
		if i > 0 {
			if v.Kind() == Pointer && v.typ_.elem.kind == Struct {
				if v.IsNil() {
					return Value{}, &embeddedNilError{v.typ_.elem.str}
				}
				v = v.Elem()
			}
		}
		v = v.Field(x)
	}
	return v, nil
}

type embeddedNilError struct{ name string }

func (e *embeddedNilError) Error() string {
	return "reflect: indirection through nil pointer to embedded struct field " + e.name
}

// FieldByName returns the struct field with the given name.
func (v Value) FieldByName(name string) Value {
	if f, ok := v.typ_.FieldByName(name); ok {
		return v.FieldByIndex(f.Index)
	}
	return Value{}
}

// FieldByNameFunc returns the struct field with a name that satisfies the
// match function.
func (v Value) FieldByNameFunc(match func(string) bool) Value {
	if f, ok := v.typ_.FieldByNameFunc(match); ok {
		return v.FieldByIndex(f.Index)
	}
	return Value{}
}

// Fields returns an iterator over each StructField of v along with its
// Value.
func (v Value) Fields() iter.Seq2[StructField, Value] {
	t := v.Type()
	if t.Kind() != Struct {
		panic("reflect: Fields of non-struct type " + t.String())
	}
	return func(yield func(StructField, Value) bool) {
		for i := range v.NumField() {
			if !yield(t.Field(i), v.Field(i)) {
				return
			}
		}
	}
}

// assignTo returns v as a value of type dst, for an assignment (Go1.27
// §Assignability), or panics.
func (v Value) assignTo(context string, dst *rtype) Value {
	switch {
	case v.typ_ == dst:
		return v
	case dst.kind == Interface:
		if v.kind() == Interface && v.IsNil() {
			return Value{typ_: dst, ptr: alloc(dst.tid), flag: 0}
		}
		if !implements(dst, v.typ_) {
			break
		}
		cell := alloc(dst.tid)
		*(*any)(cell) = valueInterface(v, false)
		return Value{typ_: dst, ptr: cell, flag: 0}
	case assignable(dst, v.typ_):
		cell := alloc(dst.tid)
		assign(dst.tid, cell, v.ptr)
		return Value{typ_: dst, ptr: cell, flag: 0}
	}
	panic(context + ": value of type " + v.typ_.str + " is not assignable to type " + dst.str)
}

// Set assigns x to the value v.
func (v Value) Set(x Value) {
	v.mustBeAssignable("reflect.Set")
	x.mustBeExported("reflect.Set")
	x = x.assignTo("reflect.Set", v.typ_)
	assign(v.typ_.tid, v.ptr, x.ptr)
}

// SetBool sets v's underlying value.
func (v Value) SetBool(x bool) {
	v.mustBeAssignable("reflect.Value.SetBool")
	v.mustBe(Bool, "reflect.Value.SetBool")
	*(*bool)(v.ptr) = x
}

// SetBytes sets v's underlying value.
func (v Value) SetBytes(x []byte) {
	v.mustBeAssignable("reflect.Value.SetBytes")
	v.mustBe(Slice, "reflect.Value.SetBytes")
	if v.typ_.elem.kind != Uint8 {
		panic("reflect.Value.SetBytes of non-byte slice")
	}
	setSlice(v.typ_.tid, v.ptr, unsafe.Pointer(unsafe.SliceData(x)), len(x), cap(x))
}

// SetInt sets v's underlying value to x.
func (v Value) SetInt(x int64) {
	v.mustBeAssignable("reflect.Value.SetInt")
	p := v.ptr
	switch v.kind() {
	case Int:
		*(*int)(p) = int(x)
	case Int8:
		*(*int8)(p) = int8(x)
	case Int16:
		*(*int16)(p) = int16(x)
	case Int32:
		*(*int32)(p) = int32(x)
	case Int64:
		*(*int64)(p) = x
	default:
		panic(&ValueError{"reflect.Value.SetInt", v.kind()})
	}
}

// SetUint sets v's underlying value to x.
func (v Value) SetUint(x uint64) {
	v.mustBeAssignable("reflect.Value.SetUint")
	p := v.ptr
	switch v.kind() {
	case Uint:
		*(*uint)(p) = uint(x)
	case Uint8:
		*(*uint8)(p) = uint8(x)
	case Uint16:
		*(*uint16)(p) = uint16(x)
	case Uint32:
		*(*uint32)(p) = uint32(x)
	case Uint64:
		*(*uint64)(p) = x
	case Uintptr:
		*(*uintptr)(p) = uintptr(x)
	default:
		panic(&ValueError{"reflect.Value.SetUint", v.kind()})
	}
}

// SetFloat sets v's underlying value to x.
func (v Value) SetFloat(x float64) {
	v.mustBeAssignable("reflect.Value.SetFloat")
	switch v.kind() {
	case Float32:
		*(*float32)(v.ptr) = float32(x)
	case Float64:
		*(*float64)(v.ptr) = x
	default:
		panic(&ValueError{"reflect.Value.SetFloat", v.kind()})
	}
}

// SetComplex sets v's underlying value to x.
func (v Value) SetComplex(x complex128) {
	v.mustBeAssignable("reflect.Value.SetComplex")
	switch v.kind() {
	case Complex64:
		*(*complex64)(v.ptr) = complex64(x)
	case Complex128:
		*(*complex128)(v.ptr) = x
	default:
		panic(&ValueError{"reflect.Value.SetComplex", v.kind()})
	}
}

// SetString sets v's underlying value to x.
func (v Value) SetString(x string) {
	v.mustBeAssignable("reflect.Value.SetString")
	v.mustBe(String, "reflect.Value.SetString")
	*(*string)(v.ptr) = x
}

// SetPointer sets the unsafe.Pointer value v to x.
func (v Value) SetPointer(x unsafe.Pointer) {
	v.mustBeAssignable("reflect.Value.SetPointer")
	v.mustBe(UnsafePointer, "reflect.Value.SetPointer")
	*(*unsafe.Pointer)(v.ptr) = x
}

// SetLen sets v's length to n.
func (v Value) SetLen(n int) {
	v.mustBeAssignable("reflect.Value.SetLen")
	v.mustBe(Slice, "reflect.Value.SetLen")
	data, _, c := sliceHeader(v.typ_.tid, v.ptr)
	if uint(n) > uint(c) {
		panic("reflect: slice length out of range in SetLen")
	}
	setSlice(v.typ_.tid, v.ptr, data, n, c)
}

// SetCap sets v's capacity to n.
func (v Value) SetCap(n int) {
	v.mustBeAssignable("reflect.Value.SetCap")
	v.mustBe(Slice, "reflect.Value.SetCap")
	data, l, c := sliceHeader(v.typ_.tid, v.ptr)
	if n < l || n > c {
		panic("reflect: slice capacity out of range in SetCap")
	}
	setSlice(v.typ_.tid, v.ptr, data, l, n)
}

// Pointer returns v's value as a uintptr.
func (v Value) Pointer() uintptr {
	return uintptr(v.pointer("reflect.Value.Pointer"))
}

// UnsafePointer returns v's value as an unsafe.Pointer.
func (v Value) UnsafePointer() unsafe.Pointer {
	return v.pointer("reflect.Value.UnsafePointer")
}

func (v Value) pointer(method string) unsafe.Pointer {
	switch v.kind() {
	case Chan, Map, Pointer, UnsafePointer, Func:
		return pointerAt(v.typ_.tid, v.ptr)
	case Slice:
		data, _, _ := sliceHeader(v.typ_.tid, v.ptr)
		return data
	case String:
		return unsafe.Pointer(unsafe.StringData(*(*string)(v.ptr)))
	}
	panic(&ValueError{method, v.kind()})
}

// Slice returns v[i:j].
func (v Value) Slice(i, j int) Value {
	switch v.kind() {
	case Slice:
		data, _, c := sliceHeader(v.typ_.tid, v.ptr)
		if i < 0 || j < i || j > c {
			panic("reflect.Value.Slice: slice index out of bounds")
		}
		cell := alloc(v.typ_.tid)
		setSlice(v.typ_.tid, cell, element(data, v.typ_.elem, i), j-i, c-i)
		return Value{typ_: v.typ_, ptr: cell, flag: v.flag & flagRO}
	case String:
		s := *(*string)(v.ptr)
		if i < 0 || j < i || j > len(s) {
			panic("reflect.Value.Slice: string slice index out of bounds")
		}
		cell := alloc(v.typ_.tid)
		*(*string)(cell) = s[i:j]
		return Value{typ_: v.typ_, ptr: cell, flag: v.flag & flagRO}
	case Array:
		if v.flag&flagAddr == 0 {
			panic("reflect.Value.Slice: slice of unaddressable array")
		}
		if i < 0 || j < i || j > v.typ_.len {
			panic("reflect.Value.Slice: slice index out of bounds")
		}
		st := SliceOf(v.typ_.elem).common()
		cell := alloc(st.tid)
		setSlice(st.tid, cell, element(v.ptr, v.typ_.elem, i), j-i, v.typ_.len-i)
		return Value{typ_: st, ptr: cell, flag: v.flag & flagRO}
	}
	panic(&ValueError{"reflect.Value.Slice", v.kind()})
}

// Slice3 is the 3-index form of the slice operation: it returns v[i:j:k].
func (v Value) Slice3(i, j, k int) Value {
	st := v.typ_
	var data unsafe.Pointer
	var c int
	switch v.kind() {
	case Slice:
		data, _, c = sliceHeader(v.typ_.tid, v.ptr)
	case Array:
		if v.flag&flagAddr == 0 {
			panic("reflect.Value.Slice3: slice of unaddressable array")
		}
		st = SliceOf(v.typ_.elem).common()
		data, c = v.ptr, v.typ_.len
	default:
		panic(&ValueError{"reflect.Value.Slice3", v.kind()})
	}
	if i < 0 || j < i || k < j || k > c {
		panic("reflect.Value.Slice3: slice index out of bounds")
	}
	cell := alloc(st.tid)
	setSlice(st.tid, cell, element(data, v.typ_.elem, i), j-i, k-i)
	return Value{typ_: st, ptr: cell, flag: v.flag & flagRO}
}

// grown returns the slice v with n more elements (zero ones), sharing the
// array of v if its capacity suffices.
func (v Value) grown(n int) Value {
	t := v.typ_
	data, l, c := sliceHeader(t.tid, v.ptr)
	cell := alloc(t.tid)
	if l+n <= c {
		setSlice(t.tid, cell, data, l+n, c)
		// The new elements are those of the array, as append keeps them
		// (they are overwritten by the callers).
		return Value{typ_: t, ptr: cell, flag: 0}
	}
	newCap := c * 2
	if newCap < l+n {
		newCap = l + n
	}
	makeSlice(t.tid, cell, l+n, newCap)
	newData, _, _ := sliceHeader(t.tid, cell)
	for i := 0; i < l; i++ {
		assign(t.elem.tid, element(newData, t.elem, i), element(data, t.elem, i))
	}
	return Value{typ_: t, ptr: cell, flag: 0}
}

// Grow increases the slice's capacity, if necessary, to guarantee space
// for another n elements.
func (v Value) Grow(n int) {
	v.mustBeAssignable("reflect.Value.Grow")
	v.mustBe(Slice, "reflect.Value.Grow")
	if n < 0 {
		panic("reflect.Value.Grow: negative len")
	}
	_, l, c := sliceHeader(v.typ_.tid, v.ptr)
	if l+n > c {
		g := v.grown(n)
		data, _, gc := sliceHeader(g.typ_.tid, g.ptr)
		setSlice(v.typ_.tid, v.ptr, data, l, gc)
	}
}

// Append appends the values x to a slice s and returns the resulting
// slice.
func Append(s Value, x ...Value) Value {
	s.mustBe(Slice, "reflect.Append")
	n := s.Len()
	s = s.grown(len(x))
	for i, v := range x {
		s.Index(n + i).Set(v)
	}
	return s
}

// AppendSlice appends a slice t to a slice s and returns the resulting
// slice.
func AppendSlice(s, t Value) Value {
	s.mustBe(Slice, "reflect.AppendSlice")
	t.mustBe(Slice, "reflect.AppendSlice")
	n := s.Len()
	m := t.Len()
	values := make([]Value, m)
	for i := 0; i < m; i++ {
		values[i] = copyOf(t.typ_.elem, t.Index(i).ptr, 0)
	}
	s = s.grown(m)
	for i := 0; i < m; i++ {
		s.Index(n + i).Set(values[i])
	}
	return s
}

// Copy copies the contents of src into dst until either dst has been
// filled or src has been exhausted. It returns the number of elements
// copied.
func Copy(dst, src Value) int {
	dk := dst.kind()
	if dk != Array && dk != Slice {
		panic(&ValueError{"reflect.Copy", dk})
	}
	if dk == Array && dst.flag&flagAddr == 0 {
		panic("reflect.Copy: unaddressable array value")
	}
	dst.mustBeExported("reflect.Copy")
	sk := src.kind()
	if sk != Array && sk != Slice && sk != String {
		panic(&ValueError{"reflect.Copy", sk})
	}
	src.mustBeExported("reflect.Copy")
	n := dst.Len()
	if m := src.Len(); m < n {
		n = m
	}
	// Copied through temporaries: the ranges may overlap.
	values := make([]Value, n)
	for i := 0; i < n; i++ {
		e := src.Index(i)
		values[i] = copyOf(e.typ_, e.ptr, 0)
	}
	for i := 0; i < n; i++ {
		d := dst.Index(i)
		assign(d.typ_.tid, d.ptr, values[i].ptr)
	}
	return n
}

// Clear clears the contents of a map or zeros the contents of a slice.
func (v Value) Clear() {
	switch v.kind() {
	case Slice:
		data, n, _ := sliceHeader(v.typ_.tid, v.ptr)
		et := v.typ_.elem
		for i := 0; i < n; i++ {
			assign(et.tid, element(data, et, i), alloc(et.tid))
		}
	case Map:
		for {
			keys, _, n := mapHeader(v.typ_.tid, v.ptr)
			if n == 0 {
				return
			}
			mapDelete(v.typ_.tid, v.ptr, keys)
		}
	default:
		panic(&ValueError{"reflect.Value.Clear", v.Kind()})
	}
}

// MapIndex returns the value associated with key in the map v (the zero
// Value if there is none).
func (v Value) MapIndex(key Value) Value {
	v.mustBe(Map, "reflect.Value.MapIndex")
	t := v.typ_
	key = key.assignTo("reflect.Value.MapIndex", t.key)
	i := mapFind(t.tid, v.ptr, key.ptr)
	if i < 0 {
		return Value{}
	}
	_, vals, _ := mapHeader(t.tid, v.ptr)
	return copyOf(t.elem, element(vals, t.elem, i), v.flag&flagRO)
}

// MapKeys returns a slice containing all the keys present in the map.
// The order is that of the model's map (gc's is unspecified).
func (v Value) MapKeys() []Value {
	v.mustBe(Map, "reflect.Value.MapKeys")
	t := v.typ_
	keys, _, n := mapHeader(t.tid, v.ptr)
	result := make([]Value, n)
	for i := 0; i < n; i++ {
		result[i] = copyOf(t.key, element(keys, t.key, i), v.flag&flagRO)
	}
	return result
}

// SetMapIndex sets the element associated with key in the map v to elem;
// the zero Value as elem deletes the key.
func (v Value) SetMapIndex(key, elem Value) {
	v.mustBe(Map, "reflect.Value.SetMapIndex")
	v.mustBeExported("reflect.Value.SetMapIndex")
	key.mustBeExported("reflect.Value.SetMapIndex")
	t := v.typ_
	key = key.assignTo("reflect.Value.SetMapIndex", t.key)
	if elem.typ_ == nil {
		mapDelete(t.tid, v.ptr, key.ptr)
		return
	}
	elem.mustBeExported("reflect.Value.SetMapIndex")
	elem = elem.assignTo("reflect.Value.SetMapIndex", t.elem)
	mapStore(t.tid, v.ptr, key.ptr, elem.ptr)
}

// A MapIter is an iterator for ranging over a map. The entries are those
// of the map when the iteration starts, in the order of the model's map.
type MapIter struct {
	m          Value
	keys, vals unsafe.Pointer
	n, i       int
	started    bool
}

// MapRange returns a range iterator for a map.
func (v Value) MapRange() *MapIter {
	if v.kind() != Map {
		panic(&ValueError{"reflect.Value.MapRange", v.kind()})
	}
	return &MapIter{m: v}
}

// Next advances the map iterator and reports whether there is another
// entry.
func (iter *MapIter) Next() bool {
	if !iter.m.IsValid() {
		panic("MapIter.Next called on an iterator that does not have an associated map Value")
	}
	if !iter.started {
		iter.started = true
		iter.keys, iter.vals, iter.n = mapHeader(iter.m.typ_.tid, iter.m.ptr)
		iter.i = 0
	} else if iter.i < iter.n {
		iter.i++
	}
	return iter.i < iter.n
}

func (iter *MapIter) check(method string) {
	if !iter.started {
		panic("MapIter." + method + " called before Next")
	}
	if iter.i >= iter.n {
		panic("MapIter." + method + " called on exhausted iterator")
	}
}

// Key returns the key of iter's current map entry.
func (iter *MapIter) Key() Value {
	iter.check("Key")
	t := iter.m.typ_
	return copyOf(t.key, element(iter.keys, t.key, iter.i), iter.m.flag&flagRO)
}

// Value returns the value of iter's current map entry.
func (iter *MapIter) Value() Value {
	iter.check("Value")
	t := iter.m.typ_
	return copyOf(t.elem, element(iter.vals, t.elem, iter.i), iter.m.flag&flagRO)
}

// Reset modifies iter to iterate over v.
func (iter *MapIter) Reset(v Value) {
	if v.IsValid() {
		v.mustBe(Map, "reflect.MapIter.Reset")
	}
	*iter = MapIter{m: v}
}

// SetIterKey assigns to v the key of iter's current map entry.
func (v Value) SetIterKey(iter *MapIter) {
	v.Set(iter.Key())
}

// SetIterValue assigns to v the value of iter's current map entry.
func (v Value) SetIterValue(iter *MapIter) {
	v.Set(iter.Value())
}

// Comparable reports whether the value v is comparable.
func (v Value) Comparable() bool {
	switch v.Kind() {
	case Invalid:
		return false
	case Array:
		for i := 0; i < v.Len(); i++ {
			if !v.Index(i).Comparable() {
				return false
			}
		}
		return true
	case Interface:
		return v.IsNil() || v.Elem().Comparable()
	case Struct:
		for i := 0; i < v.NumField(); i++ {
			if !v.Field(i).Comparable() {
				return false
			}
		}
		return true
	}
	return v.typ_.comparable
}

// Equal reports true if v is equal to u.
func (v Value) Equal(u Value) bool {
	if v.Kind() == Interface {
		v = v.Elem()
	}
	if u.Kind() == Interface {
		u = u.Elem()
	}
	if !v.IsValid() || !u.IsValid() {
		return v.IsValid() == u.IsValid()
	}
	if v.Kind() != u.Kind() || v.typ_ != u.typ_ {
		return false
	}
	switch v.Kind() {
	case Bool:
		return v.Bool() == u.Bool()
	case Int, Int8, Int16, Int32, Int64:
		return v.Int() == u.Int()
	case Uint, Uint8, Uint16, Uint32, Uint64, Uintptr:
		return v.Uint() == u.Uint()
	case Float32, Float64:
		return v.Float() == u.Float()
	case Complex64, Complex128:
		return v.Complex() == u.Complex()
	case String:
		return v.String() == u.String()
	case Chan, Pointer, UnsafePointer:
		return v.Pointer() == u.Pointer()
	case Array:
		for i := 0; i < v.Len(); i++ {
			if !v.Index(i).Equal(u.Index(i)) {
				return false
			}
		}
		return true
	case Struct:
		for i := 0; i < v.NumField(); i++ {
			if !v.Field(i).Equal(u.Field(i)) {
				return false
			}
		}
		return true
	}
	panic("reflect.Value.Equal: values of type " + v.typ_.str + " are not comparable")
}

// NumMethod returns the number of methods in the value's method set.
func (v Value) NumMethod() int {
	if v.typ_ == nil {
		panic(&ValueError{"reflect.Value.NumMethod", Invalid})
	}
	return v.typ_.NumMethod()
}

// Method returns a function value corresponding to v's i'th method.
func (v Value) Method(i int) Value {
	if v.typ_ == nil {
		panic(&ValueError{"reflect.Value.Method", Invalid})
	}
	if v.flag&flagMethod != 0 || uint(i) >= uint(v.typ_.NumMethod()) {
		panic("reflect: Method index out of range")
	}
	if v.typ_.kind == Interface {
		if v.IsNil() {
			panic("reflect: Method on nil interface value")
		}
		return v.Elem().MethodByName(v.typ_.methods[v.typ_.exportedMethod(i)].name)
	}
	j := v.typ_.exportedMethod(i)
	recv := copyOf(v.typ_, v.ptr, 0)
	return Value{typ_: v.typ_.methods[j].typ, ptr: recv.ptr,
		flag: flagMethod | v.flag&flagRO, mrecv: v.typ_, mindex: j}
}

// MethodByName returns a function value corresponding to the method of v
// with the given name (the zero Value if there is none).
func (v Value) MethodByName(name string) Value {
	if v.typ_ == nil {
		panic(&ValueError{"reflect.Value.MethodByName", Invalid})
	}
	if v.flag&flagMethod != 0 {
		panic("reflect: MethodByName of method value")
	}
	for i := 0; i < v.typ_.NumMethod(); i++ {
		if v.typ_.methods[v.typ_.exportedMethod(i)].name == name {
			return v.Method(i)
		}
	}
	return Value{}
}

// Methods returns an iterator over each Method of v along with its
// function value.
func (v Value) Methods() iter.Seq2[Method, Value] {
	return func(yield func(Method, Value) bool) {
		for i := 0; i < v.NumMethod(); i++ {
			if !yield(v.typ_.Method(i), v.Method(i)) {
				return
			}
		}
	}
}

// Call calls the function v with the input arguments in.
func (v Value) Call(in []Value) []Value {
	v.mustBe(Func, "reflect.Value.Call")
	return v.call("Call", in)
}

// CallSlice calls the variadic function v with the input arguments in,
// assigning the slice in[len(in)-1] to v's final variadic argument.
func (v Value) CallSlice(in []Value) []Value {
	v.mustBe(Func, "reflect.Value.CallSlice")
	return v.call("CallSlice", in)
}

func (v Value) call(op string, in []Value) []Value {
	t := v.typ_
	isMethod := v.flag&flagMethod != 0
	if !isMethod && pointerAt(t.tid, v.ptr) == nil {
		panic("reflect: call of nil function")
	}
	isSlice := op == "CallSlice"
	n := len(t.in)
	isVariadic := t.variadic
	if isSlice {
		if !isVariadic {
			panic("reflect: CallSlice of non-variadic function")
		}
		if len(in) < n {
			panic("reflect: CallSlice with too few input arguments")
		}
		if len(in) > n {
			panic("reflect: CallSlice with too many input arguments")
		}
	} else {
		if isVariadic {
			n--
		}
		if len(in) < n {
			panic("reflect: Call with too few input arguments")
		}
		if !isVariadic && len(in) > n {
			panic("reflect: Call with too many input arguments")
		}
	}
	for _, x := range in {
		if x.Kind() == Invalid {
			panic("reflect: " + op + " using zero Value argument")
		}
	}
	args := make([]unsafe.Pointer, len(t.in))
	for i := 0; i < n; i++ {
		args[i] = in[i].assignTo("reflect.Value.Call", t.in[i]).ptr
	}
	if !isSlice && isVariadic {
		m := len(in) - n
		slice := MakeSlice(t.in[n], m, m)
		for i := 0; i < m; i++ {
			slice.Index(i).Set(in[n+i])
		}
		args[n] = slice.ptr
	}
	out := make([]Value, len(t.out))
	results := make([]unsafe.Pointer, len(t.out))
	for j, rt := range t.out {
		out[j] = Value{typ_: rt, ptr: alloc(rt.tid)}
		results[j] = out[j].ptr
	}
	if isMethod {
		callMethod(v.mrecv.tid, v.mindex, v.ptr, args, results)
	} else {
		call(t.tid, v.ptr, args, results)
	}
	return out
}

// Send sends x on the channel v (not modelled).
func (v Value) Send(x Value) { rt.Unsupported("reflect.Value.Send") }

// Recv receives and returns a value from the channel v (not modelled).
func (v Value) Recv() (x Value, ok bool) {
	rt.Unsupported("reflect.Value.Recv")
	return
}

// TrySend attempts to send x on the channel v (not modelled).
func (v Value) TrySend(x Value) bool {
	rt.Unsupported("reflect.Value.TrySend")
	return false
}

// TryRecv attempts to receive a value from the channel v (not modelled).
func (v Value) TryRecv() (x Value, ok bool) {
	rt.Unsupported("reflect.Value.TryRecv")
	return
}

// Close closes the channel v (not modelled).
func (v Value) Close() { rt.Unsupported("reflect.Value.Close") }

// Convert returns the value v converted to type t (Go1.27 §Conversions).
func (v Value) Convert(t Type) Value {
	if v.typ_ == nil {
		panic(&ValueError{"reflect.Value.Convert", Invalid})
	}
	dst := t.common()
	if !convertible(dst, v.typ_) {
		panic("reflect.Value.Convert: value of type " + v.typ_.str + " cannot be converted to type " + dst.str)
	}
	src := v.typ_
	result := Value{typ_: dst, ptr: alloc(dst.tid), flag: flagAddr}
	switch {
	case dst.kind == Interface:
		*(*any)(result.ptr) = valueInterface(v, false)
	case isInteger(src.kind) && isInteger(dst.kind):
		if isUnsigned(src.kind) {
			result.setInteger(int64(v.Uint()))
		} else {
			result.setInteger(v.Int())
		}
	case isInteger(src.kind) && isFloat(dst.kind):
		if isUnsigned(src.kind) {
			result.SetFloat(float64(v.Uint()))
		} else {
			result.SetFloat(float64(v.Int()))
		}
	case isFloat(src.kind) && isInteger(dst.kind):
		if isUnsigned(dst.kind) {
			result.SetUint(uint64(v.Float()))
		} else {
			result.SetInt(int64(v.Float()))
		}
	case isFloat(src.kind) && isFloat(dst.kind):
		result.SetFloat(v.Float())
	case isComplex(src.kind) && isComplex(dst.kind):
		result.SetComplex(v.Complex())
	case isInteger(src.kind) && dst.kind == String:
		var r rune = 0xFFFD
		if isUnsigned(src.kind) {
			if x := v.Uint(); x <= 0x10FFFF {
				r = rune(x)
			}
		} else if x := v.Int(); x >= 0 && x <= 0x10FFFF {
			r = rune(x)
		}
		result.SetString(string(r))
	case src.kind == String && dst.kind == Slice && dst.elem.kind == Uint8:
		result.SetBytes([]byte(v.String()))
	case src.kind == String && dst.kind == Slice:
		runes := []rune(v.String())
		setSlice(dst.tid, result.ptr, unsafe.Pointer(unsafe.SliceData(runes)), len(runes), cap(runes))
	case src.kind == Slice && dst.kind == String && src.elem.kind == Uint8:
		result.SetString(string(v.Bytes()))
	case src.kind == Slice && dst.kind == String:
		data, n, _ := sliceHeader(src.tid, v.ptr)
		result.SetString(string(unsafe.Slice((*rune)(data), n)))
	case src.kind == Slice && dst.kind == Array:
		data, n, _ := sliceHeader(src.tid, v.ptr)
		if n < dst.len {
			panic("reflect: cannot convert slice with length " + itoa(n) + " to array or pointer to array with length " + itoa(dst.len))
		}
		for i := 0; i < dst.len; i++ {
			assign(dst.elem.tid, element(result.ptr, dst.elem, i), element(data, src.elem, i))
		}
	case src.kind == Slice && dst.kind == Pointer:
		data, n, _ := sliceHeader(src.tid, v.ptr)
		if n < dst.elem.len {
			panic("reflect: cannot convert slice with length " + itoa(n) + " to array or pointer to array with length " + itoa(dst.elem.len))
		}
		setPointer(dst.tid, result.ptr, data)
	case src.kind == UnsafePointer || dst.kind == UnsafePointer ||
		src.kind == Uintptr || dst.kind == Uintptr:
		rt.Unsupported("reflect.Value.Convert between pointers and unsafe.Pointer or uintptr")
	default:
		// Identical underlying types (Go1.27 §Conversions): the same
		// representation.
		assign(dst.tid, result.ptr, v.ptr)
	}
	result.flag = v.flag & flagRO
	return result
}

// setInteger stores the integer x (of 64 bits, truncated) in v.
func (v Value) setInteger(x int64) {
	if isUnsigned(v.kind()) {
		v.SetUint(uint64(x))
	} else {
		v.SetInt(x)
	}
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	neg := n < 0
	if neg {
		n = -n
	}
	var b []byte
	for n > 0 {
		b = append([]byte{byte('0' + n%10)}, b...)
		n /= 10
	}
	if neg {
		return "-" + string(b)
	}
	return string(b)
}

// CanConvert reports whether the value v can be converted to type t.
func (v Value) CanConvert(t Type) bool {
	vt := v.Type()
	if !vt.ConvertibleTo(t) {
		return false
	}
	switch {
	case vt.Kind() == Slice && t.Kind() == Array:
		if t.Len() > v.Len() {
			return false
		}
	case vt.Kind() == Slice && t.Kind() == Pointer && t.Elem().Kind() == Array:
		n := t.Elem().Len()
		if n > v.Len() {
			return false
		}
	}
	return true
}

// OverflowComplex reports whether the complex128 x cannot be represented
// by v's type.
func (v Value) OverflowComplex(x complex128) bool {
	return v.Type().OverflowComplex(x)
}

// OverflowFloat reports whether the float64 x cannot be represented by
// v's type.
func (v Value) OverflowFloat(x float64) bool {
	return v.Type().OverflowFloat(x)
}

// OverflowInt reports whether the int64 x cannot be represented by v's
// type.
func (v Value) OverflowInt(x int64) bool {
	return v.Type().OverflowInt(x)
}

// OverflowUint reports whether the uint64 x cannot be represented by v's
// type.
func (v Value) OverflowUint(x uint64) bool {
	return v.Type().OverflowUint(x)
}

// Seq returns an iter.Seq[Value] that loops over the elements of v.
func (v Value) Seq() iter.Seq[Value] {
	switch v.Kind() {
	case Int, Int8, Int16, Int32, Int64:
		return func(yield func(Value) bool) {
			n := v.Int()
			for i := int64(0); i < n; i++ {
				x := New(v.typ_).Elem()
				x.SetInt(i)
				if !yield(copyOf(x.typ_, x.ptr, 0)) {
					return
				}
			}
		}
	case Uint, Uint8, Uint16, Uint32, Uint64, Uintptr:
		return func(yield func(Value) bool) {
			n := v.Uint()
			for i := uint64(0); i < n; i++ {
				x := New(v.typ_).Elem()
				x.SetUint(i)
				if !yield(copyOf(x.typ_, x.ptr, 0)) {
					return
				}
			}
		}
	case Array, Slice, String:
		return func(yield func(Value) bool) {
			for i := 0; i < v.Len(); i++ {
				x := New(TypeOf(0)).Elem()
				x.SetInt(int64(i))
				if !yield(copyOf(x.typ_, x.ptr, 0)) {
					return
				}
			}
		}
	case Map:
		return func(yield func(Value) bool) {
			for _, k := range v.MapKeys() {
				if !yield(k) {
					return
				}
			}
		}
	}
	rt.Unsupported("reflect.Value.Seq of this kind")
	return nil
}

// Seq2 returns an iter.Seq2[Value, Value] that loops over the elements of
// v.
func (v Value) Seq2() iter.Seq2[Value, Value] {
	switch v.Kind() {
	case Array, Slice:
		return func(yield func(Value, Value) bool) {
			for i := 0; i < v.Len(); i++ {
				x := New(TypeOf(0)).Elem()
				x.SetInt(int64(i))
				if !yield(copyOf(x.typ_, x.ptr, 0), v.Index(i)) {
					return
				}
			}
		}
	case Map:
		return func(yield func(Value, Value) bool) {
			for _, k := range v.MapKeys() {
				if !yield(k, v.MapIndex(k)) {
					return
				}
			}
		}
	}
	rt.Unsupported("reflect.Value.Seq2 of this kind")
	return nil
}
