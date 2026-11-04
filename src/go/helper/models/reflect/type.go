// Package reflect models the standard library's reflect (go1.27) for
// verification. It replaces the real package (see the helper's models.go):
// programs are type-checked against this API.
//
// A type is described by a descriptor (rtype) that the front-end
// generates from the type table for each type a reflected value may have
// (src/go/go_reflect.cpp); type identity is identity of descriptors. A
// Value is the descriptor of its type, a pointer to the value, and flags.
// Operations that depend on the type of a value beyond what the
// descriptor says (copying a value, making an interface value of it, map
// operations) are intrinsics, whose bodies the front-end generates (see
// intrinsics.go). Operations the model does not support report so (a
// failing property of class "unsupported").
package reflect

import (
	"cprover/rt"
	"iter"
	"strconv"
)

// A Kind represents the specific kind of type that a Type represents.
type Kind uint

const (
	Invalid Kind = iota
	Bool
	Int
	Int8
	Int16
	Int32
	Int64
	Uint
	Uint8
	Uint16
	Uint32
	Uint64
	Uintptr
	Float32
	Float64
	Complex64
	Complex128
	Array
	Chan
	Func
	Interface
	Map
	Pointer
	Slice
	String
	Struct
	UnsafePointer
)

// Ptr is the old name for the Pointer kind.
const Ptr = Pointer

// String returns the name of k.
func (k Kind) String() string {
	switch k {
	case Invalid:
		return "invalid"
	case Bool:
		return "bool"
	case Int:
		return "int"
	case Int8:
		return "int8"
	case Int16:
		return "int16"
	case Int32:
		return "int32"
	case Int64:
		return "int64"
	case Uint:
		return "uint"
	case Uint8:
		return "uint8"
	case Uint16:
		return "uint16"
	case Uint32:
		return "uint32"
	case Uint64:
		return "uint64"
	case Uintptr:
		return "uintptr"
	case Float32:
		return "float32"
	case Float64:
		return "float64"
	case Complex64:
		return "complex64"
	case Complex128:
		return "complex128"
	case Array:
		return "array"
	case Chan:
		return "chan"
	case Func:
		return "func"
	case Interface:
		return "interface"
	case Map:
		return "map"
	case Pointer:
		return "ptr"
	case Slice:
		return "slice"
	case String:
		return "string"
	case Struct:
		return "struct"
	case UnsafePointer:
		return "unsafe.Pointer"
	}
	return "kind" + strconv.Itoa(int(k))
}

// ChanDir represents a channel type's direction.
type ChanDir int

const (
	RecvDir ChanDir = 1 << iota
	SendDir
	BothDir = RecvDir | SendDir
)

// String returns the spelling of d in channel types.
func (d ChanDir) String() string {
	switch d {
	case SendDir:
		return "chan<-"
	case RecvDir:
		return "<-chan"
	case BothDir:
		return "chan"
	}
	return "ChanDir" + strconv.Itoa(int(d))
}

// rtype is the descriptor of a type (built by the front-end, which fills
// in these fields by name).
type rtype struct {
	tid        int32 // the front-end's type id
	kind       Kind
	str        string // Type.String
	name       string // Type.Name
	pkgPath    string // Type.PkgPath
	size       uintptr
	align      int
	fieldAlign int
	stride     uintptr // the front-end's size of a value (address arithmetic)
	comparable bool
	elem       *rtype // of arrays, channels, maps, pointers and slices
	key        *rtype // of maps
	ptrTo      *rtype // the pointer type to this type, if described
	under      *rtype // the underlying type
	len        int    // of arrays
	dir        ChanDir
	variadic   bool
	in, out    []*rtype
	fields     []structField
	methods    []method // sorted by name
}

type structField struct {
	name     string
	pkgPath  string
	typ      *rtype
	tag      StructTag
	offset   uintptr // gc's
	off      uintptr // the front-end's
	embedded bool
}

type method struct {
	name    string
	pkgPath string
	typ     *rtype // the type of the method value (without receiver)
	sig     int32  // the type id of that type
}

// Type is the representation of a Go type.
type Type interface {
	Align() int
	FieldAlign() int
	Method(int) Method
	Methods() iter.Seq[Method]
	MethodByName(string) (Method, bool)
	NumMethod() int
	Name() string
	PkgPath() string
	Size() uintptr
	String() string
	Kind() Kind
	Implements(u Type) bool
	AssignableTo(u Type) bool
	ConvertibleTo(u Type) bool
	Comparable() bool
	Bits() int
	ChanDir() ChanDir
	IsVariadic() bool
	Elem() Type
	Field(i int) StructField
	Fields() iter.Seq[StructField]
	FieldByIndex(index []int) StructField
	FieldByName(name string) (StructField, bool)
	FieldByNameFunc(match func(string) bool) (StructField, bool)
	In(i int) Type
	Ins() iter.Seq[Type]
	Key() Type
	Len() int
	NumField() int
	NumIn() int
	NumOut() int
	Out(i int) Type
	Outs() iter.Seq[Type]
	OverflowComplex(x complex128) bool
	OverflowFloat(x float64) bool
	OverflowInt(x int64) bool
	OverflowUint(x uint64) bool
	CanSeq() bool
	CanSeq2() bool
	common() *rtype
}

// toType returns t as a Type (nil for nil).
func toType(t *rtype) Type {
	if t == nil {
		return nil
	}
	return t
}

func (t *rtype) common() *rtype { return t }

// TypeOf returns the dynamic type of i, nil if i is nil.
func TypeOf(i any) Type {
	return toType(typeOf(ifaceType(i)))
}

// TypeFor returns the Type that represents the type argument T.
func TypeFor[T any]() Type {
	var v T
	if t := TypeOf(v); t != nil {
		return t
	}
	return TypeOf((*T)(nil)).Elem()
}

// PtrTo is the old name of PointerTo.
func PtrTo(t Type) Type { return PointerTo(t) }

// PointerTo returns the pointer type with element t.
func PointerTo(t Type) Type {
	p := t.common().ptrTo
	if p == nil {
		rt.Unsupported("reflect.PointerTo of a type whose pointer type the program does not have")
	}
	return p
}

// SliceOf returns the slice type with element type t.
func SliceOf(t Type) Type {
	rt.Unsupported("reflect.SliceOf")
	return nil
}

// MapOf returns the map type with the given key and element types.
func MapOf(key, elem Type) Type {
	rt.Unsupported("reflect.MapOf")
	return nil
}

// ArrayOf returns the array type with the given length and element type.
func ArrayOf(length int, elem Type) Type {
	rt.Unsupported("reflect.ArrayOf")
	return nil
}

// ChanOf returns the channel type with the given direction and element
// type.
func ChanOf(dir ChanDir, t Type) Type {
	rt.Unsupported("reflect.ChanOf")
	return nil
}

// FuncOf returns the function type with the given argument and result
// types.
func FuncOf(in, out []Type, variadic bool) Type {
	rt.Unsupported("reflect.FuncOf")
	return nil
}

// StructOf returns the struct type containing fields.
func StructOf(fields []StructField) Type {
	rt.Unsupported("reflect.StructOf")
	return nil
}

func (t *rtype) Align() int      { return t.align }
func (t *rtype) FieldAlign() int { return t.fieldAlign }
func (t *rtype) Name() string    { return t.name }
func (t *rtype) PkgPath() string { return t.pkgPath }
func (t *rtype) Size() uintptr   { return t.size }
func (t *rtype) String() string  { return t.str }
func (t *rtype) Kind() Kind      { return t.kind }

func (t *rtype) Comparable() bool { return t.comparable }

func (t *rtype) Bits() int {
	k := t.kind
	if k < Int || k > Complex128 {
		panic("reflect: Bits of non-arithmetic Type " + t.str)
	}
	return int(t.size) * 8
}

func (t *rtype) ChanDir() ChanDir {
	if t.kind != Chan {
		panic("reflect: ChanDir of non-chan type " + t.str)
	}
	return t.dir
}

func (t *rtype) IsVariadic() bool {
	if t.kind != Func {
		panic("reflect: IsVariadic of non-func type " + t.str)
	}
	return t.variadic
}

func (t *rtype) Elem() Type {
	switch t.kind {
	case Array, Chan, Map, Pointer, Slice:
		if t.elem == nil {
			rt.Unsupported("reflect.Type.Elem of a type the model does not describe")
		}
		return t.elem
	}
	panic("reflect: Elem of invalid type " + t.str)
}

func (t *rtype) Key() Type {
	if t.kind != Map {
		panic("reflect: Key of non-map type " + t.str)
	}
	return toType(t.key)
}

func (t *rtype) Len() int {
	if t.kind != Array {
		panic("reflect: Len of non-array type " + t.str)
	}
	return t.len
}

func (t *rtype) NumField() int {
	if t.kind != Struct {
		panic("reflect: NumField of non-struct type " + t.str)
	}
	return len(t.fields)
}

func (t *rtype) field(i int) StructField {
	if i < 0 || i >= len(t.fields) {
		panic("reflect: Field index out of bounds")
	}
	p := &t.fields[i]
	return StructField{Name: p.name, PkgPath: p.pkgPath, Type: toType(p.typ),
		Tag: p.tag, Offset: p.offset, Index: []int{i}, Anonymous: p.embedded}
}

func (t *rtype) Field(i int) StructField {
	if t.kind != Struct {
		panic("reflect: Field of non-struct type " + t.str)
	}
	return t.field(i)
}

func (t *rtype) Fields() iter.Seq[StructField] {
	if t.kind != Struct {
		panic("reflect: Fields of non-struct type " + t.str)
	}
	return func(yield func(StructField) bool) {
		for i := range t.fields {
			if !yield(t.field(i)) {
				return
			}
		}
	}
}

func (t *rtype) FieldByIndex(index []int) StructField {
	if t.kind != Struct {
		panic("reflect: FieldByIndex of non-struct type " + t.str)
	}
	var f StructField
	var cur *rtype = t
	for i, x := range index {
		if i > 0 {
			if cur.kind == Pointer && cur.elem.kind == Struct {
				cur = cur.elem
			}
		}
		f = cur.field(x)
		cur = f.Type.common()
	}
	f.Index = append([]int(nil), index...)
	return f
}

func (t *rtype) FieldByName(name string) (StructField, bool) {
	return t.FieldByNameFunc(func(s string) bool { return s == name })
}

// FieldByNameFunc searches breadth-first, as reflect does: the field at
// the shallowest depth, through embedded structs (and pointers to them);
// several at that depth make it ambiguous (not found).
func (t *rtype) FieldByNameFunc(match func(string) bool) (StructField, bool) {
	if t.kind != Struct {
		panic("reflect: FieldByNameFunc of non-struct type " + t.str)
	}
	type candidate struct {
		typ   *rtype
		index []int
	}
	current := []candidate{{t, nil}}
	visited := map[*rtype]bool{}
	for len(current) > 0 {
		var next []candidate
		var result StructField
		count := 0
		for _, c := range current {
			if visited[c.typ] {
				continue
			}
			visited[c.typ] = true
			for i := range c.typ.fields {
				f := &c.typ.fields[i]
				if match(f.name) {
					count++
					if count == 1 {
						result = c.typ.field(i)
						result.Index = append(append([]int(nil), c.index...), i)
					}
					continue
				}
				if f.embedded {
					ft := f.typ
					if ft.kind == Pointer {
						ft = ft.elem
					}
					if ft != nil && ft.kind == Struct {
						next = append(next, candidate{ft,
							append(append([]int(nil), c.index...), i)})
					}
				}
			}
		}
		if count == 1 {
			return result, true
		}
		if count > 1 {
			return StructField{}, false
		}
		current = next
	}
	return StructField{}, false
}

func (t *rtype) NumIn() int {
	if t.kind != Func {
		panic("reflect: NumIn of non-func type " + t.str)
	}
	return len(t.in)
}

func (t *rtype) In(i int) Type {
	if t.kind != Func {
		panic("reflect: In of non-func type " + t.str)
	}
	return toType(t.in[i])
}

func (t *rtype) Ins() iter.Seq[Type] {
	if t.kind != Func {
		panic("reflect: Ins of non-func type " + t.str)
	}
	return func(yield func(Type) bool) {
		for _, x := range t.in {
			if !yield(toType(x)) {
				return
			}
		}
	}
}

func (t *rtype) NumOut() int {
	if t.kind != Func {
		panic("reflect: NumOut of non-func type " + t.str)
	}
	return len(t.out)
}

func (t *rtype) Out(i int) Type {
	if t.kind != Func {
		panic("reflect: Out of non-func type " + t.str)
	}
	return toType(t.out[i])
}

func (t *rtype) Outs() iter.Seq[Type] {
	if t.kind != Func {
		panic("reflect: Outs of non-func type " + t.str)
	}
	return func(yield func(Type) bool) {
		for _, x := range t.out {
			if !yield(toType(x)) {
				return
			}
		}
	}
}

// exportedMethod returns the index in t.methods of the i-th method that
// reflect lists (all of an interface's, the exported ones otherwise), or
// -1.
func (t *rtype) exportedMethod(i int) int {
	n := 0
	for j := range t.methods {
		if t.kind == Interface || t.methods[j].pkgPath == "" {
			if n == i {
				return j
			}
			n++
		}
	}
	return -1
}

func (t *rtype) NumMethod() int {
	n := 0
	for j := range t.methods {
		if t.kind == Interface || t.methods[j].pkgPath == "" {
			n++
		}
	}
	return n
}

// Method returns the i-th method. For non-interface types, Type and Func
// (the method with the receiver as first parameter) are not modelled
// (nil and the zero Value).
func (t *rtype) Method(i int) Method {
	j := t.exportedMethod(i)
	if j < 0 {
		panic("reflect: Method index out of range")
	}
	m := &t.methods[j]
	result := Method{Name: m.name, PkgPath: m.pkgPath, Index: i}
	if t.kind == Interface {
		result.Type = toType(m.typ)
	}
	return result
}

func (t *rtype) Methods() iter.Seq[Method] {
	return func(yield func(Method) bool) {
		for i := 0; i < t.NumMethod(); i++ {
			if !yield(t.Method(i)) {
				return
			}
		}
	}
}

func (t *rtype) MethodByName(name string) (Method, bool) {
	for i := 0; i < t.NumMethod(); i++ {
		if t.methods[t.exportedMethod(i)].name == name {
			return t.Method(i), true
		}
	}
	return Method{}, false
}

// implements reports whether t has all methods of interface type u
// (Go1.27 §Implementing_an_interface).
func implements(u, t *rtype) bool {
	if u.kind != Interface {
		return false
	}
	for i := range u.methods {
		um := &u.methods[i]
		found := false
		for j := range t.methods {
			tm := &t.methods[j]
			if tm.name == um.name && tm.sig == um.sig &&
				(t.kind == Interface || tm.pkgPath == "" ||
					tm.pkgPath == u.pkgPath) {
				found = true
				break
			}
		}
		if !found {
			return false
		}
	}
	return true
}

func (t *rtype) Implements(u Type) bool {
	if u == nil {
		panic("reflect: nil type passed to Type.Implements")
	}
	if u.Kind() != Interface {
		panic("reflect: non-interface type passed to Type.Implements")
	}
	return implements(u.common(), t)
}

// assignable reports whether a value of type t is assignable to type u
// (Go1.27 §Assignability, for values that are not untyped constants).
func assignable(u, t *rtype) bool {
	if u == t {
		return true
	}
	if u.kind == Interface {
		return implements(u, t)
	}
	// Identical underlying types, and at least one of them not named.
	if (u.name == "" || t.name == "") && u.under == t.under &&
		u.under != nil {
		return true
	}
	// A bidirectional channel to a channel type of the same element type.
	if u.kind == Chan && t.kind == Chan && t.dir == BothDir &&
		u.elem == t.elem && (u.name == "" || t.name == "") {
		return true
	}
	return false
}

func (t *rtype) AssignableTo(u Type) bool {
	if u == nil {
		panic("reflect: nil type passed to Type.AssignableTo")
	}
	return assignable(u.common(), t)
}

func isNumeric(k Kind) bool  { return k >= Int && k <= Complex128 }
func isInteger(k Kind) bool  { return k >= Int && k <= Uintptr }
func isFloat(k Kind) bool    { return k == Float32 || k == Float64 }
func isComplex(k Kind) bool  { return k == Complex64 || k == Complex128 }
func isUnsigned(k Kind) bool { return k >= Uint && k <= Uintptr }

// convertible reports whether a value of type t can be converted to type
// u (Go1.27 §Conversions, for non-constant values).
func convertible(u, t *rtype) bool {
	if assignable(u, t) {
		return true
	}
	uk, tk := u.kind, t.kind
	if u.under == t.under && u.under != nil {
		return true
	}
	// Unnamed pointer types to identical underlying base types.
	if uk == Pointer && tk == Pointer && u.name == "" && t.name == "" &&
		u.elem != nil && t.elem != nil && u.elem.under == t.elem.under {
		return true
	}
	switch {
	case isInteger(tk) || isFloat(tk):
		if isInteger(uk) || isFloat(uk) {
			return true
		}
		if isInteger(tk) && uk == String {
			return true
		}
	case isComplex(tk):
		return isComplex(uk)
	case tk == String:
		if uk == Slice && u.elem != nil && u.elem.pkgPath == "" &&
			(u.elem.kind == Uint8 || u.elem.kind == Int32) {
			return true
		}
	case tk == Slice:
		if uk == String && t.elem != nil && t.elem.pkgPath == "" &&
			(t.elem.kind == Uint8 || t.elem.kind == Int32) {
			return true
		}
		if uk == Array && u.elem == t.elem {
			return true
		}
		if uk == Pointer && u.elem != nil && u.elem.kind == Array &&
			u.elem.elem == t.elem {
			return true
		}
	case tk == UnsafePointer:
		return uk == Pointer || uk == Uintptr
	case tk == Pointer:
		return uk == UnsafePointer
	case tk == Uintptr:
		return uk == UnsafePointer
	}
	if tk == Uintptr && uk == UnsafePointer {
		return true
	}
	return false
}

func (t *rtype) ConvertibleTo(u Type) bool {
	if u == nil {
		panic("reflect: nil type passed to Type.ConvertibleTo")
	}
	return convertible(u.common(), t)
}

func (t *rtype) OverflowComplex(x complex128) bool {
	switch t.kind {
	case Complex64:
		return overflowFloat32(real(x)) || overflowFloat32(imag(x))
	case Complex128:
		return false
	}
	panic("reflect: OverflowComplex of non-complex type " + t.str)
}

func (t *rtype) OverflowFloat(x float64) bool {
	switch t.kind {
	case Float32:
		return overflowFloat32(x)
	case Float64:
		return false
	}
	panic("reflect: OverflowFloat of non-float type " + t.str)
}

func (t *rtype) OverflowInt(x int64) bool {
	switch t.kind {
	case Int, Int8, Int16, Int32, Int64:
		bitSize := t.size * 8
		trunc := (x << (64 - bitSize)) >> (64 - bitSize)
		return x != trunc
	}
	panic("reflect: OverflowInt of non-int type " + t.str)
}

func (t *rtype) OverflowUint(x uint64) bool {
	switch t.kind {
	case Uint, Uintptr, Uint8, Uint16, Uint32, Uint64:
		bitSize := t.size * 8
		trunc := (x << (64 - bitSize)) >> (64 - bitSize)
		return x != trunc
	}
	panic("reflect: OverflowUint of non-uint type " + t.str)
}

func overflowFloat32(x float64) bool {
	if x < 0 {
		x = -x
	}
	return 3.40282346638528859811704183484516925440e+38 < x &&
		x <= 1.79769313486231570814527423731704356798070e+308
}

func (t *rtype) CanSeq() bool {
	switch t.kind {
	case Int8, Int16, Int32, Int64, Int, Uint8, Uint16, Uint32, Uint64,
		Uint, Uintptr, Array, Slice, Chan, String, Map:
		return true
	case Func:
		return t.canRangeFunc(1)
	case Pointer:
		return t.elem != nil && t.elem.kind == Array
	}
	return false
}

func (t *rtype) CanSeq2() bool {
	switch t.kind {
	case Array, Slice, String, Map:
		return true
	case Func:
		return t.canRangeFunc(2)
	case Pointer:
		return t.elem != nil && t.elem.kind == Array
	}
	return false
}

// canRangeFunc reports whether t is func(yield func(K[, V]) bool), with n
// parameters of yield.
func (t *rtype) canRangeFunc(n int) bool {
	if len(t.in) != 1 || len(t.out) != 0 {
		return false
	}
	y := t.in[0]
	return y != nil && y.kind == Func && len(y.in) == n && len(y.out) == 1 &&
		y.out[0] != nil && y.out[0].kind == Bool
}

// Method represents a single method.
type Method struct {
	// Name is the method name.
	Name string

	// PkgPath is the package path that qualifies a lower case (unexported)
	// method name. It is empty for upper case (exported) method names.
	PkgPath string

	Type  Type  // method type
	Func  Value // func with receiver as first argument
	Index int   // index for Type.Method
}

// IsExported reports whether the method is exported.
func (m Method) IsExported() bool {
	return m.PkgPath == ""
}

// A StructField describes a single field in a struct.
type StructField struct {
	// Name is the field name.
	Name string

	// PkgPath is the package path that qualifies a lower case (unexported)
	// field name. It is empty for upper case (exported) field names.
	PkgPath string

	Type      Type      // field type
	Tag       StructTag // field tag string
	Offset    uintptr   // offset within struct, in bytes
	Index     []int     // index sequence for Type.FieldByIndex
	Anonymous bool      // is an embedded field
}

// IsExported reports whether the field is exported.
func (f StructField) IsExported() bool {
	return f.PkgPath == ""
}

// A StructTag is the tag string in a struct field.
type StructTag string

// Get returns the value associated with key in the tag string.
func (tag StructTag) Get(key string) string {
	v, _ := tag.Lookup(key)
	return v
}

// Lookup returns the value associated with key in the tag string (as
// reflect's).
func (tag StructTag) Lookup(key string) (value string, ok bool) {
	for tag != "" {
		// Skip leading space.
		i := 0
		for i < len(tag) && tag[i] == ' ' {
			i++
		}
		tag = tag[i:]
		if tag == "" {
			break
		}

		// Scan to colon. A space, a quote or a control character is a
		// syntax error.
		i = 0
		for i < len(tag) && tag[i] > ' ' && tag[i] != ':' && tag[i] != '"' && tag[i] != 0x7f {
			i++
		}
		if i == 0 || i+1 >= len(tag) || tag[i] != ':' || tag[i+1] != '"' {
			break
		}
		name := string(tag[:i])
		tag = tag[i+1:]

		// Scan quoted string to find value.
		i = 1
		for i < len(tag) && tag[i] != '"' {
			if tag[i] == '\\' {
				i++
			}
			i++
		}
		if i >= len(tag) {
			break
		}
		qvalue := string(tag[:i+1])
		tag = tag[i+1:]

		if key == name {
			value, err := strconv.Unquote(qvalue)
			if err != nil {
				break
			}
			return value, true
		}
	}
	return "", false
}

// VisibleFields returns all the visible fields in t, which must be a
// struct type (as reflect's).
func VisibleFields(t Type) []StructField {
	if t == nil {
		panic("reflect: VisibleFields(nil)")
	}
	if t.Kind() != Struct {
		panic("reflect.VisibleFields of non-struct type")
	}
	w := &visibleFieldsWalker{
		byName:   make(map[string]int),
		visiting: make(map[Type]bool),
		fields:   make([]StructField, 0, t.NumField()),
		index:    make([]int, 0, 2),
	}
	w.walk(t)
	// Remove all the fields that have been hidden.
	j := 0
	for i := range w.fields {
		f := &w.fields[i]
		if f.Name == "" {
			continue
		}
		if i != j {
			w.fields[j] = *f
		}
		j++
	}
	return w.fields[:j]
}

type visibleFieldsWalker struct {
	byName   map[string]int
	visiting map[Type]bool
	fields   []StructField
	index    []int
}

func (w *visibleFieldsWalker) walk(t Type) {
	if w.visiting[t] {
		return
	}
	w.visiting[t] = true
	for i := 0; i < t.NumField(); i++ {
		f := t.Field(i)
		w.index = append(w.index, i)
		add := true
		if oldIndex, ok := w.byName[f.Name]; ok {
			old := &w.fields[oldIndex]
			if len(w.index) == len(old.Index) {
				old.Name = ""
				add = false
			} else if len(w.index) < len(old.Index) {
				old.Name = ""
			} else {
				add = false
			}
		}
		if add {
			f.Index = append([]int(nil), w.index...)
			w.byName[f.Name] = len(w.fields)
			w.fields = append(w.fields, f)
		}
		if f.Anonymous {
			if f.Type.Kind() == Pointer {
				f.Type = f.Type.Elem()
			}
			if f.Type.Kind() == Struct {
				w.walk(f.Type)
			}
		}
		w.index = w.index[:len(w.index)-1]
	}
	delete(w.visiting, t)
}

// A ValueError occurs when a Value method is invoked on a Value that does
// not support it.
type ValueError struct {
	Method string
	Kind   Kind
}

func (e *ValueError) Error() string {
	if e.Kind == 0 {
		return "reflect: call of " + e.Method + " on zero Value"
	}
	return "reflect: call of " + e.Method + " on " + e.Kind.String() + " Value"
}

// SliceHeader is the runtime representation of a slice (deprecated in
// reflect; the model's slices have another representation).
type SliceHeader struct {
	Data uintptr
	Len  int
	Cap  int
}

// StringHeader is the runtime representation of a string (deprecated in
// reflect; the model's strings have another representation).
type StringHeader struct {
	Data uintptr
	Len  int
}

// A SelectDir describes the communication direction of a select case.
type SelectDir int

const (
	_ SelectDir = iota
	SelectSend
	SelectRecv
	SelectDefault
)

// A SelectCase describes a single case in a select operation.
type SelectCase struct {
	Dir  SelectDir // direction of case
	Chan Value     // channel to use (for send or receive)
	Send Value     // value to send (for send)
}

// Select executes a select operation (not modelled).
func Select(cases []SelectCase) (chosen int, recv Value, recvOK bool) {
	rt.Unsupported("reflect.Select")
	return
}
