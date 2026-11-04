// cbmc-go-helper parses and type-checks a Go package with go/parser and
// go/types, and writes it as JSON for CBMC's Go front-end (src/go/).
//
// The C++ side loads the JSON into its in-memory Go AST and never redoes
// name resolution, constant evaluation or type inference: those are taken
// from go/types, the reference implementation of the Go specification
// (doc/architectural/go-frontend-plan.md §4).
//
// Usage: cbmc-go-helper -goarch ARCH -o OUT.json FILE.go...
//
//	cbmc-go-helper -syntax FILE.go...    (only check the syntax)
//
// Exit status: 0 on success, 2 on usage or I/O errors, 3 on syntax or type
// errors (reported on stderr as "file:line:col: message").
//
// Output schema (version 1):
//
//	{ "version": 1, "go_version": "go1.27.3", "goarch": "amd64",
//	  "compiler": {"name": "gc", "version": "go1.27.3"},
//	  "package": "main", "files": ["/abs/main.go", ...],
//	  "asts": [ <File node>, ... ],
//	  "types": [ <type>, ... ],          // referenced by index
//	  "objects": { "<id>": <object> },   // referenced by id
//	  "packages": [ {"path": "<import path>", "init_order":
//	      [ {"lhs": ["<id>", ...], "rhs": <expr node>}, ... ]}, ... ] }
//
// The files given are the program's package; "files" and "asts" also have
// the files of the packages converted from source with it (see
// packages.go): the user packages it imports, models of the standard
// library (as "cprover/std/<path>"), and the front-end's runtime package
// "cprover/rt" (runtime/rt.go), whose objects have ids "cprover/rt.<name>".
// "packages" lists them in the order of their initialisation.
//
// A node is {"_k": <go/ast type name>, "_p": [file, line, col], <fields>}
// plus, where go/types provides them:
//
//	"_t"    index of the expression's type (types.Info.Types), after
//	        untyped constants have been given their final type
//	"_cv"   exact constant value (go/constant ExactString) and
//	"_cvk"  its kind: "bool", "int", "float", "complex", or "string"
//	"_tx"   true if the expression denotes a type (types.TypeAndValue.IsType)
//	"_obj"  id of the object an identifier denotes; "_def" if it defines it
//	"_sel"  selection info for selector expressions
//	"_inst" type-argument indices for instantiated generic identifiers,
//	        whose "_obj" (and "_sel" "obj") is then the instance
//	"_impl" id of an implicitly declared object (e.g. per-clause type-switch
//	        variables, types.Info.Implicits)
//	"_sb"   for string constants, their bytes
//
// Type entries also have "tid", the index of the first identical type, and
// non-interface types their method set "mset" (see completeTypes).
//
//	"_fb"   for constants of floating-point type, the IEEE 754 bits of the
//	        value rounded to the type; "_fbr"/"_fbi" for complex types
//
// Generic code is monomorphised: generic functions and methods, and generic
// type declarations, are not emitted as such. Each instance of a generic
// function or method is a FuncDecl of its own, appended to the declarations
// of its file, with object id `<pkg>.<f>[<type args>]` (methods:
// `<pkg>.<T>[<type args>].<m>`, generic methods `...<m>[<type args>]`),
// in which every type is substituted and objects declared in the generic
// declaration have ids `<id>@<instance id>`. Named type entries of
// instantiated types have "iname", `<type object id>[<type args>]`.
package main

import (
	_ "embed"
	"encoding/json"
	"flag"
	"fmt"
	"go/ast"
	"go/build"
	"go/constant"
	"go/importer"
	"go/parser"
	"go/token"
	"go/types"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"reflect"
	"runtime"
	"strconv"
	"strings"
)

// The verification-primitives package. The same file is a real Go package,
// so programs that import "cprover" still build with the Go toolchain when
// it is placed on their import path.
//
//go:embed cprover/cprover.go
var cproverSource string

const cproverPath = "cprover"

// The front-end's runtime support (runtime/rt.go), type-checked as package
// "cprover/rt" and emitted with the program, whose conversion calls it.
//
//go:embed runtime/rt.go
var runtimeSource string

const runtimePath = "cprover/rt"

// importer that serves "cprover" from the embedded source, user packages
// from source (see loader), and the standard library from the gc
// toolchain's export data, loading models of it where available.
type helperImporter struct {
	fset    *token.FileSet
	sizes   types.Sizes
	std     types.Importer
	cprover *types.Package
	loader  *loader
}

func (h *helperImporter) Import(path string) (*types.Package, error) {
	if path == runtimePath {
		return h.loader.runtime()
	}
	if path != cproverPath {
		// The model is loaded (as path, see loadReplacement), but packages
		// are type-checked against the real package.
		if checkedAgainstReal[path] {
			if _, err := h.loader.loadReplacement(path); err != nil {
				return nil, err
			}
			pkg, err := h.std.Import(path)
			if err == nil {
				h.loader.real[path] = pkg
			}
			return pkg, err
		}
		if pkg, ok, err := h.loader.importSource(path); ok {
			return pkg, err
		}
		if replaced[path] {
			return h.loader.loadReplacement(path)
		}
		pkg, err := h.std.Import(path)
		if err == nil {
			h.loader.real[path] = pkg
			err = h.loader.loadModel(path)
		}
		return pkg, err
	}
	if h.cprover == nil {
		f, err := parser.ParseFile(h.fset, "cprover/cprover.go", cproverSource,
			parser.SkipObjectResolution)
		if err != nil {
			return nil, err
		}
		conf := types.Config{Importer: h.std, Sizes: h.sizes}
		pkg, err := conf.Check(cproverPath, h.fset, []*ast.File{f}, nil)
		if err != nil {
			return nil, err
		}
		h.cprover = pkg
	}
	return h.cprover, nil
}

type encoder struct {
	fset     *token.FileSet
	sizes    types.Sizes
	info     *types.Info
	fileIdx  map[string]int
	types    []any
	typeList []types.Type // the type of each entry of types
	typeIdx  map[types.Type]int
	// The entries of composite types, by type string (see typ).
	structural map[string][]int
	objects    map[string]any
	ids        map[types.Object]string

	// completeTypes state: the number of entries completed, and the
	// entries that are candidates for identity, by key.
	completed int
	byKey     map[string][]int

	// Monomorphisation (see instance). ctxt is shared with the type checker
	// so that identical instantiations are the same types.Type.
	ctxt         *types.Context
	cur          *instance // the instance being encoded, or nil
	instances    map[string]*instance
	queue        []*instance
	genericDecls map[*types.Func]*ast.FuncDecl
	declFile     map[*ast.FuncDecl]int
	localIDs     map[localKey]string
	aliases      map[types.Object]string     // instances' copies of local objects
	selObj       map[*ast.Ident]types.Object // re-resolved selectors and keys
	selPath      map[*ast.SelectorExpr]selPath
	loader       *loader
	namedTypes   []types.Type // the types named by typeName,
	typeNames    []string     // and their names
	usedNames    map[string]bool
	reflectUsed  []string // see reflectUses
	// The entries that reflectInfo adds, and their depth (see
	// derivedType).
	reflectDerived map[int]int
	replacedCache  map[types.Type]types.Type // see replaced
}

// An instance of a generic function or method (Go1.27 §Instantiations),
// encoded as a FuncDecl of its own: the generic declaration with every
// type parameter replaced by its type argument (monomorphisation). Objects
// declared in the generic declaration get ids of their own in each
// instance (`<id>@<instance id>`), with their types substituted.
type instance struct {
	id     string
	origin *types.Func
	decl   *ast.FuncDecl // nil if the declaration is in another package
	params []*types.TypeParam
	args   []types.Type
	memo   map[types.Type]types.Type
}

type localKey struct {
	obj  types.Object
	inst *instance
}

// maxInstances bounds the number of instances of generic functions and
// methods, which recursive generic code can make unbounded.
const maxInstances = 2000

func (in *instance) isLocal(o types.Object) bool {
	return in != nil && in.decl != nil && o.Pos().IsValid() &&
		in.decl.Pos() <= o.Pos() && o.Pos() < in.decl.End()
}

// objID gives every object a unique id. Package-level objects and methods
// get readable ids ("main.f", "main.T.M"); objects in local scopes get a
// numeric suffix ("main.x#3") because shadowing makes names ambiguous.
func (e *encoder) objID(o types.Object) string {
	if e.cur != nil && o == types.Object(e.cur.origin) {
		return e.cur.id
	}
	if id, ok := e.aliases[o]; ok {
		return id
	}
	if f, ok := o.(*types.Func); ok && e.loader != nil {
		// A method of a real type whose package has a source model is the
		// model's (see realType).
		if m := e.loader.modelMethod(f); m != nil {
			o = m
		}
	}
	pkg := ""
	if o.Pkg() != nil {
		pkg = o.Pkg().Path()
	}
	if f, ok := o.(*types.Func); ok && isMethod(f) {
		if n := recvNamed(f); n != nil && n.TypeArgs().Len() > 0 &&
			!types.IsInterface(n) {
			return e.methodInstance(f, nil)
		}
	}
	if e.cur.isLocal(o) {
		key := localKey{o, e.cur}
		if id, ok := e.localIDs[key]; ok {
			return id
		}
		id := fmt.Sprintf("%s.%s#%d@%s", pkg, o.Name(), len(e.localIDs),
			e.cur.id)
		e.localIDs[key] = id
		e.objects[id] = e.objectEntry(o)
		return id
	}
	// Objects of generic declarations may also occur outside instances, in
	// the types of instances from go/types (which shares the variables
	// whose types do not change); there they get plain ids.
	if id, ok := e.ids[o]; ok {
		return id
	}
	var id string
	switch {
	case o.Pkg() == nil: // universe: predeclared types, builtins, nil, ...
		id = o.Name()
	case isMethod(o):
		recv := o.(*types.Func).Signature().Recv().Type()
		if p, ok := recv.(*types.Pointer); ok {
			recv = p.Elem()
		}
		name := "?"
		if n, ok := types.Unalias(recv).(*types.Named); ok {
			name = n.Obj().Name()
		}
		id = pkg + "." + name + "." + o.Name()
	case o.Name() == "init" && isFunc(o):
		// Go1.27 §Package_initialization: a package may declare several
		// init functions, which cannot be referred to; each needs its own
		// id. (go/types does not insert them into the package scope.)
		id = fmt.Sprintf("%s.init#%d", pkg, len(e.ids))
	case o.Parent() == o.Pkg().Scope():
		id = pkg + "." + o.Name()
	default:
		id = fmt.Sprintf("%s.%s#%d", pkg, o.Name(), len(e.ids))
	}
	e.ids[o] = id
	e.objects[id] = e.objectEntry(o)
	return id
}

// recvNamed is the named type of a method's receiver.
func recvNamed(f *types.Func) *types.Named {
	recv := f.Signature().Recv().Type()
	if p, ok := recv.(*types.Pointer); ok {
		recv = p.Elem()
	}
	n, _ := types.Unalias(recv).(*types.Named)
	return n
}

// typeName names a type in instance ids, the same for identical types
// (so that, e.g., an alias and the type it denotes give one instance): the
// string, with package paths, of the first identical type named, made
// unique among non-identical types (e.g., local types of the same name) by
// a suffix.
func (e *encoder) typeName(t types.Type) string {
	for i, u := range e.namedTypes {
		if types.Identical(u, t) {
			return e.typeNames[i]
		}
	}
	s := types.TypeString(e.unaliased(t), nil)
	name := s
	for n := 1; e.usedNames[name]; n++ {
		name = fmt.Sprintf("%s'%d", s, n) // '#' is for local objects
	}
	e.usedNames[name] = true
	e.namedTypes = append(e.namedTypes, t)
	e.typeNames = append(e.typeNames, name)
	return name
}

// unaliased is t with the aliases in it replaced by the types they denote
// (Go1.27 §Alias_declarations), for naming.
func (e *encoder) unaliased(t types.Type) types.Type {
	switch x := types.Unalias(t).(type) {
	case *types.Pointer:
		return types.NewPointer(e.unaliased(x.Elem()))
	case *types.Slice:
		return types.NewSlice(e.unaliased(x.Elem()))
	case *types.Array:
		return types.NewArray(e.unaliased(x.Elem()), x.Len())
	case *types.Map:
		return types.NewMap(e.unaliased(x.Key()), e.unaliased(x.Elem()))
	case *types.Chan:
		return types.NewChan(x.Dir(), e.unaliased(x.Elem()))
	case *types.Named:
		args := typeList(x.TypeArgs())
		if len(args) == 0 {
			return x
		}
		for i, a := range args {
			args[i] = e.unaliased(a)
		}
		if r, err := types.Instantiate(e.ctxt, x.Origin(), args,
			false); err == nil {
			return r
		}
		return x
	default:
		return x
	}
}

func (e *encoder) typeArgs(args []types.Type) string {
	var names []string
	for _, a := range args {
		names = append(names, e.typeName(a))
	}
	return "[" + strings.Join(names, ",") + "]"
}

func typeList(l *types.TypeList) []types.Type {
	var out []types.Type
	for i := 0; i < l.Len(); i++ {
		out = append(out, l.At(i))
	}
	return out
}

func typeParams(l *types.TypeParamList) []*types.TypeParam {
	var out []*types.TypeParam
	for i := 0; i < l.Len(); i++ {
		out = append(out, l.At(i))
	}
	return out
}

// funcInstance returns the object id of generic function f instantiated
// with targs, `<pkg>.<f>[<targs>]`, creating the instance if needed.
func (e *encoder) funcInstance(f *types.Func, targs []types.Type) string {
	id := f.Pkg().Path() + "." + f.Name() + e.typeArgs(targs)
	return e.instantiate(id, f, typeParams(f.Signature().TypeParams()), targs)
}

// methodInstance returns the object id of method m of an instantiated
// generic type (`<pkg>.<T>[<targs>].<m>`), where m is that instance's
// method, instantiated with its own type arguments mtargs if it is a
// generic method (`...<m>[<mtargs>]`, Go1.27 §Method_declarations).
func (e *encoder) methodInstance(m *types.Func, mtargs []types.Type) string {
	n := recvNamed(m)
	rargs := typeList(n.TypeArgs())
	id := m.Pkg().Path() + "." + n.Obj().Name()
	if len(rargs) > 0 {
		id += e.typeArgs(rargs)
	}
	id += "." + m.Name()
	if len(mtargs) > 0 {
		id += e.typeArgs(mtargs)
	}
	origin := m.Origin()
	sig := origin.Signature()
	params := append(typeParams(sig.RecvTypeParams()),
		typeParams(sig.TypeParams())...)
	return e.instantiate(id, origin, params, append(rargs, mtargs...))
}

// instantiate registers the instance id of origin with the type arguments
// args for params, with an object entry of the substituted signature, and
// queues its declaration (if it is available) for encoding.
func (e *encoder) instantiate(id string, origin *types.Func,
	params []*types.TypeParam, args []types.Type) string {
	if _, ok := e.instances[id]; ok {
		return id
	}
	if len(e.instances) >= maxInstances {
		fmt.Fprintf(os.Stderr, "%s: more than %d instances of generic "+
			"functions and methods (unbounded instantiation?)\n",
			e.fset.Position(origin.Pos()), maxInstances)
		os.Exit(3)
	}
	inst := &instance{id: id, origin: origin, decl: e.genericDecls[origin],
		params: params, args: args, memo: map[types.Type]types.Type{}}
	e.instances[id] = inst
	saved := e.cur
	e.cur = inst
	// The name has the type arguments of the function or method itself.
	name := origin.Name()
	if own := len(args) - origin.Signature().RecvTypeParams().Len(); own > 0 {
		name += e.typeArgs(args[len(args)-own:])
	}
	e.objects[id] = map[string]any{"name": name,
		"exported": origin.Exported(), "pkg": origin.Pkg().Path(),
		"package_level": !isMethod(origin), "kind": "func",
		"type": e.typ(e.subst(origin.Type()))}
	e.cur = saved
	if inst.decl != nil {
		e.queue = append(e.queue, inst)
	}
	return id
}

// subst replaces the type parameters of the current instance in t by its
// type arguments. Types that contain objects declared in the generic
// declaration (parameters, fields of local struct types) are rebuilt with
// that instance's objects, so that type entries and identifiers agree.
func (e *encoder) subst(t types.Type) types.Type {
	if e.cur == nil || t == nil {
		return t
	}
	if r, ok := e.cur.memo[t]; ok {
		return r
	}
	r := e.substType(t)
	e.cur.memo[t] = r
	return r
}

func (e *encoder) substType(t types.Type) types.Type {
	switch x := t.(type) {
	case *types.TypeParam:
		for i, p := range e.cur.params {
			if p == x {
				return e.cur.args[i]
			}
		}
	case *types.Alias:
		return e.subst(types.Unalias(t))
	case *types.Named:
		args := typeList(x.TypeArgs())
		changed := false
		for i, a := range args {
			args[i] = e.subst(a)
			changed = changed || args[i] != a
		}
		if changed {
			if r, err := types.Instantiate(e.ctxt, x.Origin(), args,
				false); err == nil {
				return r
			}
		}
	case *types.Pointer:
		if el := e.subst(x.Elem()); el != x.Elem() {
			return types.NewPointer(el)
		}
	case *types.Slice:
		if el := e.subst(x.Elem()); el != x.Elem() {
			return types.NewSlice(el)
		}
	case *types.Array:
		if el := e.subst(x.Elem()); el != x.Elem() {
			return types.NewArray(el, x.Len())
		}
	case *types.Map:
		k, v := e.subst(x.Key()), e.subst(x.Elem())
		if k != x.Key() || v != x.Elem() {
			return types.NewMap(k, v)
		}
	case *types.Chan:
		if el := e.subst(x.Elem()); el != x.Elem() {
			return types.NewChan(x.Dir(), el)
		}
	case *types.Struct:
		changed := false
		var fields []*types.Var
		var tags []string
		for i := 0; i < x.NumFields(); i++ {
			f := x.Field(i)
			ft := e.subst(f.Type())
			nf := f
			if ft != f.Type() || e.cur.isLocal(f) {
				changed = true
				nf = types.NewField(f.Pos(), f.Pkg(), f.Name(), ft, f.Embedded())
				if e.cur.isLocal(f) {
					e.aliases[nf] = e.objID(f)
				}
			}
			fields = append(fields, nf)
			tags = append(tags, x.Tag(i))
		}
		if changed {
			return types.NewStruct(fields, tags)
		}
	case *types.Tuple:
		if r, changed := e.substTuple(x); changed {
			return r
		}
	case *types.Signature:
		return e.substSignature(x)
	case *types.Interface:
		changed := false
		var methods []*types.Func
		for i := 0; i < x.NumExplicitMethods(); i++ {
			// The receiver of the new methods is set by NewInterfaceType.
			m := x.ExplicitMethod(i)
			params, c1 := e.substTuple(m.Signature().Params())
			results, c2 := e.substTuple(m.Signature().Results())
			if c1 || c2 {
				changed = true
				m = types.NewFunc(m.Pos(), m.Pkg(), m.Name(),
					types.NewSignatureType(nil, nil, nil, params, results,
						m.Signature().Variadic()))
			}
			methods = append(methods, m)
		}
		var embedded []types.Type
		for i := 0; i < x.NumEmbeddeds(); i++ {
			et := x.EmbeddedType(i)
			if _, isUnion := et.(*types.Union); !isUnion {
				et = e.subst(et)
			}
			changed = changed || et != x.EmbeddedType(i)
			embedded = append(embedded, et)
		}
		if changed {
			return types.NewInterfaceType(methods, embedded).Complete()
		}
	}
	return t
}

// substVar substitutes in the type of v; v is replaced if its type changes
// or it is declared in the current generic declaration (then the new
// variable has the instance's id of v).
func (e *encoder) substVar(v *types.Var) (*types.Var, bool) {
	vt := e.subst(v.Type())
	local := e.cur.isLocal(v)
	if vt == v.Type() && !local {
		return v, false
	}
	nv := types.NewParam(v.Pos(), v.Pkg(), v.Name(), vt)
	if local && v.Name() != "" {
		e.aliases[nv] = e.objID(v)
	}
	return nv, true
}

func (e *encoder) substTuple(t *types.Tuple) (*types.Tuple, bool) {
	if t == nil {
		return nil, false
	}
	changed := false
	var vars []*types.Var
	for i := 0; i < t.Len(); i++ {
		v, c := e.substVar(t.At(i))
		changed = changed || c
		vars = append(vars, v)
	}
	if !changed {
		return t, false
	}
	return types.NewTuple(vars...), true
}

// substSignature substitutes in a signature, which loses its type
// parameters (an instance is not generic).
func (e *encoder) substSignature(s *types.Signature) *types.Signature {
	params, c1 := e.substTuple(s.Params())
	results, c2 := e.substTuple(s.Results())
	recv, c3 := s.Recv(), false
	if recv != nil {
		recv, c3 = e.substVar(recv)
	}
	if !c1 && !c2 && !c3 && s.TypeParams().Len() == 0 &&
		s.RecvTypeParams().Len() == 0 {
		return s
	}
	return types.NewSignatureType(recv, nil, nil, params, results,
		s.Variadic())
}

func isMethod(o types.Object) bool {
	f, ok := o.(*types.Func)
	return ok && f.Signature().Recv() != nil
}

func isFunc(o types.Object) bool {
	_, ok := o.(*types.Func)
	return ok && !isMethod(o)
}

func (e *encoder) objectEntry(o types.Object) map[string]any {
	entry := map[string]any{"name": o.Name(), "exported": o.Exported()}
	if o.Pkg() != nil {
		entry["pkg"] = o.Pkg().Path()
		entry["package_level"] = o.Parent() == o.Pkg().Scope()
	}
	switch x := o.(type) {
	case *types.Var:
		if x.IsField() {
			entry["kind"] = "field"
		} else {
			entry["kind"] = "var"
		}
	case *types.Func:
		entry["kind"] = "func"
		if x.Signature().TypeParams().Len() > 0 {
			return entry // generic, only used instantiated
		}
	case *types.Const:
		entry["kind"] = "const"
		entry["value"] = x.Val().ExactString()
	case *types.TypeName:
		entry["kind"] = "type"
		if n, ok := x.Type().(*types.Named); ok && n.TypeParams().Len() > 0 &&
			n.TypeArgs().Len() == 0 {
			return entry // a generic type, only used instantiated
		}
	case *types.PkgName:
		entry["kind"] = "package"
		entry["path"] = x.Imported().Path()
	case *types.Builtin:
		entry["kind"] = "builtin"
	case *types.Nil:
		entry["kind"] = "nil"
	case *types.Label:
		entry["kind"] = "label"
	default:
		entry["kind"] = "other"
	}
	if o.Type() != nil {
		entry["type"] = e.typ(e.subst(o.Type()))
	}
	return entry
}

var basicNames = map[types.BasicKind]string{
	types.Invalid: "invalid", types.Bool: "bool", types.Int: "int",
	types.Int8: "int8", types.Int16: "int16", types.Int32: "int32",
	types.Int64: "int64", types.Uint: "uint", types.Uint8: "uint8",
	types.Uint16: "uint16", types.Uint32: "uint32", types.Uint64: "uint64",
	types.Uintptr: "uintptr", types.Float32: "float32",
	types.Float64: "float64", types.Complex64: "complex64",
	types.Complex128: "complex128", types.String: "string",
	types.UnsafePointer: "unsafe.Pointer", types.UntypedBool: "untyped bool",
	types.UntypedInt: "untyped int", types.UntypedRune: "untyped rune",
	types.UntypedFloat:   "untyped float",
	types.UntypedComplex: "untyped complex",
	types.UntypedString:  "untyped string", types.UntypedNil: "untyped nil",
}

// typ returns the index of t in the type table, adding it (and the types
// it refers to) if necessary. The index is reserved before the entry is
// built so that recursive types terminate.
func (e *encoder) typ(t types.Type) int {
	t = e.realType(types.Unalias(t))
	if i, ok := e.typeIdx[t]; ok {
		return i
	}
	// go/types creates a type literal for each occurrence (e.g., *T for
	// each &x): identical composite types without objects of their own
	// share one entry, with one method set.
	var key string
	switch t.(type) {
	case *types.Pointer, *types.Slice, *types.Array, *types.Map, *types.Chan:
		key = t.String()
		for _, j := range e.structural[key] {
			if e.identical(e.typeList[j], t) {
				e.typeIdx[t] = j
				return j
			}
		}
	}
	i := len(e.types)
	if key != "" {
		e.structural[key] = append(e.structural[key], i)
	}
	e.typeIdx[t] = i
	e.types = append(e.types, nil)
	e.typeList = append(e.typeList, t)
	var entry map[string]any
	switch x := t.(type) {
	case *types.Basic:
		entry = map[string]any{"k": "basic", "kind": basicNames[x.Kind()]}
	case *types.Named:
		if e.cur.isLocal(x.Obj()) {
			// Its underlying type would need substituting as well.
			entry = map[string]any{"k": "unknown",
				"reason": "type declarations in generic functions"}
			break
		}
		entry = map[string]any{"k": "named", "obj": e.objID(x.Obj()),
			"under": e.typ(x.Underlying())}
		if args := x.TypeArgs(); args.Len() > 0 {
			var targs []int
			for j := 0; j < args.Len(); j++ {
				targs = append(targs, e.typ(args.At(j)))
			}
			entry["targs"] = targs
			// Go1.27 §Instantiations: each instance is a distinct type.
			entry["iname"] = e.objID(x.Obj()) + e.typeArgs(typeList(args))
		}
	case *types.Pointer:
		entry = map[string]any{"k": "pointer", "elem": e.typ(x.Elem())}
	case *types.Slice:
		entry = map[string]any{"k": "slice", "elem": e.typ(x.Elem())}
		// gc's size of the elements and whether they have pointers, on
		// which gc's capacities of append depend (rt.GrowCapGC).
		if size, pointers, ok := e.gcLayout(x.Elem()); ok {
			entry["esize"] = size
			entry["eptrs"] = pointers
		}
	case *types.Array:
		entry = map[string]any{"k": "array", "elem": e.typ(x.Elem()),
			"len": x.Len()}
	case *types.Map:
		entry = map[string]any{"k": "map", "key": e.typ(x.Key()),
			"elem": e.typ(x.Elem())}
	case *types.Chan:
		entry = map[string]any{"k": "chan", "elem": e.typ(x.Elem()),
			"dir": [...]string{"both", "send", "recv"}[x.Dir()]}
	case *types.Struct:
		var fields []any
		for j := 0; j < x.NumFields(); j++ {
			f := x.Field(j)
			field := map[string]any{"name": f.Name(),
				"type": e.typ(f.Type()), "embedded": f.Embedded(),
				"obj": e.objID(f)}
			if tag := x.Tag(j); tag != "" {
				field["tag"] = tag
			}
			fields = append(fields, field)
		}
		entry = map[string]any{"k": "struct", "fields": fields}
	case *types.Tuple:
		entry = map[string]any{"k": "tuple", "items": e.vars(x)}
	case *types.Signature:
		entry = map[string]any{"k": "signature", "params": e.vars(x.Params()),
			"results": e.vars(x.Results()), "variadic": x.Variadic()}
		if r := x.Recv(); r != nil {
			entry["recv"] = e.vars(types.NewTuple(r))[0]
		}
	case *types.Interface:
		var methods []any
		for j := 0; j < x.NumMethods(); j++ {
			m := x.Method(j)
			methods = append(methods, map[string]any{"name": m.Name(),
				"type": e.typ(m.Type())})
		}
		entry = map[string]any{"k": "interface", "methods": methods,
			"comparable": x.IsComparable(), "method_set": x.IsMethodSet()}
	case *types.TypeParam:
		entry = map[string]any{"k": "typeparam", "name": x.Obj().Name(),
			"index": x.Index(), "constraint": e.typ(x.Constraint())}
	default:
		entry = map[string]any{"k": "unknown", "text": t.String()}
	}
	entry["str"] = t.String()
	e.types[i] = entry
	return i
}

// completeTypes adds to every type entry its canonical index "tid" (the
// first entry of an identical type, Go1.27 §Type_identity: go/types may
// represent identical types by distinct objects) and, for non-interface
// types, its method set "mset" (Go1.27 §Method_sets): for each method its
// name, the method object, the selection path through embedded fields
// (ending in the method's index), whether pointer indirections occur on
// the way, whether the method has a pointer receiver, and the type of the
// method value (its signature without receiver). Method sets may add types
// to the table, which are completed in turn.
func (e *encoder) completeTypes() {
	// Candidates for identity share a key; parameter names do not matter
	// for identity (Go1.27 §Type_identity), so signatures are keyed by
	// their arity only.
	key := func(t types.Type) string {
		if s, ok := t.(*types.Signature); ok {
			return fmt.Sprintf("func/%d/%d/%v", s.Params().Len(),
				s.Results().Len(), s.Variadic())
		}
		return t.String()
	}
	byKey := e.byKey
	for i := e.completed; i < len(e.types); i++ { // e.types may grow
		e.completed = i + 1
		t := e.typeList[i]
		entry := e.types[i].(map[string]any)
		canonical := i
		for _, j := range byKey[key(t)] {
			if e.identical(e.typeList[j], t) {
				canonical = j
				break
			}
		}
		if canonical == i {
			byKey[key(t)] = append(byKey[key(t)], i)
		}
		entry["tid"] = canonical
		if canonical == i && e.reflects() {
			e.reflectInfo(t, entry)
		}
		if types.IsInterface(t) {
			continue
		}
		if _, isParam := t.(*types.TypeParam); isParam {
			continue
		}
		mset := types.NewMethodSet(t)
		var methods []any
		for j := 0; j < mset.Len(); j++ {
			sel := mset.At(j)
			f := sel.Obj().(*types.Func)
			if f.Signature().TypeParams().Len() > 0 {
				// Generic methods must be instantiated to be used and are
				// not in method sets of interfaces (Go1.27 §Interface_types),
				// so dynamic dispatch never needs them.
				continue
			}
			_, ptrRecv := f.Signature().Recv().Type().(*types.Pointer)
			methods = append(methods, map[string]any{
				"name": f.Name(), "obj": e.objID(f), "path": sel.Index(),
				"indirect": sel.Indirect(), "ptr_recv": ptrRecv,
				"type": e.typ(sel.Type())})
		}
		if methods != nil {
			entry["mset"] = methods
		}
	}
}

func (e *encoder) vars(t *types.Tuple) []any {
	out := []any{}
	for j := 0; j < t.Len(); j++ {
		v := t.At(j)
		item := map[string]any{"name": v.Name(), "type": e.typ(v.Type())}
		if v.Name() != "" {
			item["obj"] = e.objID(v)
		}
		out = append(out, item)
	}
	return out
}

var constKinds = map[constant.Kind]string{constant.Bool: "bool",
	constant.Int: "int", constant.Float: "float",
	constant.Complex: "complex", constant.String: "string"}

func (e *encoder) node(n ast.Node) any {
	if n == nil || reflect.ValueOf(n).IsNil() {
		return nil
	}
	if c, ok := n.(*ast.CompositeLit); ok {
		if out := e.constantArray(c); out != nil {
			return out
		}
	}
	v := reflect.ValueOf(n).Elem()
	out := map[string]any{"_k": v.Type().Name()}
	if p := e.fset.Position(n.Pos()); p.IsValid() {
		out["_p"] = []int{e.fileIdx[p.Filename], p.Line, p.Column}
	}
	e.resolveInInstance(n)
	// The operands of a constant expression (e.g., of a concatenation of
	// thousands of string literals in a generated table) are not needed:
	// the converter uses its value ("_cv", below). Encoding them would
	// repeat the value of each partial result.
	skipFields := false
	switch x := n.(type) {
	case *ast.BinaryExpr, *ast.ParenExpr, *ast.UnaryExpr:
		if tv, ok := e.info.Types[x.(ast.Expr)]; ok && tv.Value != nil {
			skipFields = true
		}
	}
	for i := 0; i < v.NumField() && !skipFields; i++ {
		f, name := v.Field(i), v.Type().Field(i).Name
		switch name {
		case "Obj", "Scope", "Unresolved", "Imports", "Comments", "Doc",
			"Comment":
			continue // superseded by go/types, or irrelevant
		case "TypeParams":
			if e.cur != nil {
				continue // an instance is not generic
			}
		}
		if r := e.value(f); r != nil {
			out[name] = r
		}
	}
	if x, ok := n.(ast.Expr); ok {
		if tv, ok := e.info.Types[x]; ok {
			// Instantiated identifiers and selectors have the type of the
			// instance, not that of the generic function or type.
			var instID *ast.Ident
			switch y := x.(type) {
			case *ast.Ident:
				instID = y
			case *ast.SelectorExpr:
				instID = y.Sel
			}
			if inst, ok := e.info.Instances[instID]; ok && instID != nil {
				tv.Type = inst.Type
			}
			tv.Type = e.subst(tv.Type)
			out["_t"] = e.typ(tv.Type)
			if tv.IsType() {
				out["_tx"] = true
			}
			if tv.Value == nil {
				if size, ok := e.instanceSize(n); ok {
					// Go1.27 §Package_unsafe: "The functions Alignof
					// and Sizeof take an expression x of any type";
					// constant in an instance.
					out["_cv"] = strconv.FormatInt(size, 10)
					out["_cvk"] = "int"
				}
			}
			if tv.Value != nil {
				out["_cv"] = tv.Value.ExactString()
				out["_cvk"] = constKinds[tv.Value.Kind()]
				e.floatConstant(out, tv)
				if tv.Value.Kind() == constant.String {
					// The exact bytes (Go strings need not be valid UTF-8).
					sb := []int{}
					for _, b := range []byte(constant.StringVal(tv.Value)) {
						sb = append(sb, int(b))
					}
					out["_sb"] = sb
				}
			}
		}
	}
	if id, ok := n.(*ast.Ident); ok {
		o := e.info.Uses[id]
		if d := e.info.Defs[id]; d != nil {
			o = d
			out["_def"] = true
		}
		if r, ok := e.selObj[id]; ok {
			o = r
		}
		if f, ok := o.(*types.Func); ok && e.loader != nil {
			// A standard-library function with a model denotes the model.
			if m := e.loader.model(f); m != nil {
				o = m
			}
		}
		if v, ok := o.(*types.Var); ok && e.loader != nil {
			// So does a package-level variable.
			if m := e.loader.modelVar(v); m != nil {
				o = m
			}
		}
		if inst, ok := e.info.Instances[id]; ok {
			// Go1.27 §Instantiations: "A generic function, method, or type
			// is instantiated by substituting type arguments for the type
			// parameters". Instantiated functions and methods denote their
			// instance.
			var targs []types.Type
			var tidx []int
			for i := 0; i < inst.TypeArgs.Len(); i++ {
				targs = append(targs, e.subst(inst.TypeArgs.At(i)))
				tidx = append(tidx, e.typ(targs[i]))
			}
			out["_inst"] = tidx
			out["_t"] = e.typ(e.subst(inst.Type)) // even if not in Types
			f, _ := o.(*types.Func)
			if f != nil && isMethod(f) {
				out["_obj"] = e.methodInstance(f, targs)
			} else if m := e.genericModel(f, inst); m != nil {
				out["_obj"] = e.funcInstance(m, targs)
			} else if f != nil && e.genericDecls[f] != nil {
				out["_obj"] = e.funcInstance(f, targs)
			} else if o != nil {
				// Generic types, and functions without source here (e.g.,
				// cprover.Nondet, which the converter implements).
				out["_obj"] = e.objID(o)
			}
		} else if o != nil {
			out["_obj"] = e.objID(o)
		}
	}
	if o := e.info.Implicits[n]; o != nil {
		out["_impl"] = e.objID(o)
	}
	// Go1.27 §Passing_arguments_to_..._parameters: f(xs...) passes a slice
	// as the variadic parameter. The position-typed field is dropped by
	// value(), so record it explicitly.
	if c, ok := n.(*ast.CallExpr); ok && c.Ellipsis.IsValid() {
		out["_ellipsis"] = true
	}
	if c, ok := n.(*ast.CallExpr); ok && e.loader != nil {
		e.errorfCall(c, out)
	}
	if se, ok := n.(*ast.SelectorExpr); ok {
		if s := e.info.Selections[se]; s != nil {
			index, indirect := s.Index(), s.Indirect()
			if r, ok := e.selPath[se]; ok {
				index, indirect = r.index, r.indirect
			}
			// The selector's object, as encoded for Sel (an instance for
			// instantiated generic methods).
			sel := out["Sel"].(map[string]any)
			out["_sel"] = map[string]any{
				"kind":     [...]string{"field", "method", "methodexpr"}[s.Kind()],
				"path":     index,
				"indirect": indirect,
				"obj":      sel["_obj"],
			}
		}
	}
	return out
}

type selPath struct {
	index    []int
	indirect bool
}

// resolveInInstance resolves, in an instance, the selectors and the keys
// of struct literals whose operand type involves type parameters, in the
// substituted type: their objects and paths are those of the instance's
// types (methods of the type argument, fields of instantiated structs).
func (e *encoder) resolveInInstance(n ast.Node) {
	switch x := n.(type) {
	case *ast.SelectorExpr:
		s := e.info.Selections[x]
		if s == nil {
			return
		}
		// The selected object (for methods of instantiated types, the
		// instance's method, which Uses need not record).
		e.selObj[x.Sel] = s.Obj()
		delete(e.selPath, x)
		recv := e.subst(s.Recv())
		if recv == s.Recv() {
			return
		}
		obj, index, indirect := types.LookupFieldOrMethod(recv, true,
			s.Obj().Pkg(), s.Obj().Name())
		if obj == nil {
			return
		}
		e.selObj[x.Sel] = obj
		e.selPath[x] = selPath{index, indirect}
	case *ast.CompositeLit:
		tv, ok := e.info.Types[x]
		if !ok || e.cur == nil {
			return
		}
		t := e.subst(tv.Type)
		if t == tv.Type {
			return
		}
		if p, ok := t.Underlying().(*types.Pointer); ok {
			t = p.Elem() // elided &T
		}
		if _, ok := t.Underlying().(*types.Struct); !ok {
			return
		}
		for _, elt := range x.Elts {
			kv, ok := elt.(*ast.KeyValueExpr)
			if !ok {
				continue
			}
			key, ok := kv.Key.(*ast.Ident)
			if !ok || e.info.Uses[key] == nil {
				continue
			}
			obj, _, _ := types.LookupFieldOrMethod(t, false,
				e.info.Uses[key].Pkg(), key.Name)
			if obj != nil {
				e.selObj[key] = obj
			}
		}
	}
}

// floatConstant records constants of floating-point and complex types as
// the IEEE 754 bits of their value rounded to the type ("_fb", or "_fbr"
// and "_fbi" for the parts of complex values), and constants of integer
// types given by floating-point constants (e.g. 1e3) as integers. Go1.27
// §Constants: numeric constants are exact; §Representability: a constant
// of floating-point type is rounded to that type's precision.
func (e *encoder) floatConstant(out map[string]any, tv types.TypeAndValue) {
	basic, ok := tv.Type.Underlying().(*types.Basic)
	if !ok || tv.Value.Kind() == constant.Bool ||
		tv.Value.Kind() == constant.String {
		return
	}
	bits := func(v constant.Value, single bool) string {
		if single {
			f, _ := constant.Float32Val(constant.ToFloat(v))
			return strconv.FormatUint(uint64(math.Float32bits(f)), 10)
		}
		f, _ := constant.Float64Val(constant.ToFloat(v))
		return strconv.FormatUint(math.Float64bits(f), 10)
	}
	switch {
	case basic.Info()&types.IsInteger != 0 &&
		tv.Value.Kind() != constant.Int:
		out["_cv"] = constant.ToInt(tv.Value).ExactString()
		out["_cvk"] = "int"
	case basic.Info()&types.IsFloat != 0:
		out["_fb"] = bits(tv.Value, basic.Kind() == types.Float32)
		out["_cvk"] = "float"
	case basic.Info()&types.IsComplex != 0:
		single := basic.Kind() == types.Complex64
		v := constant.ToComplex(tv.Value)
		out["_fbr"] = bits(constant.Real(v), single)
		out["_fbi"] = bits(constant.Imag(v), single)
		out["_cvk"] = "complex"
	}
}

func (e *encoder) value(f reflect.Value) any {
	switch f.Kind() {
	case reflect.Interface, reflect.Pointer:
		if f.IsNil() {
			return nil
		}
		if n, ok := f.Interface().(ast.Node); ok {
			return e.node(n)
		}
	case reflect.Slice:
		if f.Len() == 0 {
			return nil
		}
		xs := []any{}
		for j := 0; j < f.Len(); j++ {
			xs = append(xs, e.value(f.Index(j)))
		}
		return xs
	case reflect.String:
		if s := f.String(); s != "" {
			return s
		}
	case reflect.Bool:
		if f.Bool() {
			return true
		}
	case reflect.Int:
		switch f.Type() {
		case reflect.TypeOf(token.ILLEGAL):
			return token.Token(f.Int()).String()
		case reflect.TypeOf(ast.SEND):
			return int(f.Int())
		}
	}
	return nil
}

// withoutGenerics returns file f (with index i) without its generic
// declarations, which are recorded instead: they are encoded once per
// instance (see instance).
func (e *encoder) withoutGenerics(f *ast.File, i int) *ast.File {
	out := *f
	out.Decls = nil
	for _, d := range f.Decls {
		switch x := d.(type) {
		case *ast.FuncDecl:
			fn, _ := e.info.Defs[x.Name].(*types.Func)
			if fn != nil && (fn.Signature().TypeParams().Len() > 0 ||
				fn.Signature().RecvTypeParams().Len() > 0) {
				e.genericDecls[fn] = x
				e.declFile[x] = i
				continue
			}
		case *ast.GenDecl:
			g := *x
			g.Specs = nil
			for _, s := range x.Specs {
				if ts, ok := s.(*ast.TypeSpec); ok && ts.TypeParams != nil {
					continue // generic types are used instantiated
				}
				g.Specs = append(g.Specs, s)
			}
			d = &g
		}
		out.Decls = append(out.Decls, d)
	}
	return &out
}

// instanceSize returns the value of n if it is a call of unsafe.Sizeof or
// unsafe.Alignof in an instance of a generic function, whose argument's
// type then is known (go/types leaves them non-constant when the type
// depends on type parameters).
func (e *encoder) instanceSize(n ast.Node) (int64, bool) {
	call, ok := n.(*ast.CallExpr)
	if !ok || e.cur == nil || len(call.Args) != 1 {
		return 0, false
	}
	var id *ast.Ident
	switch f := ast.Unparen(call.Fun).(type) {
	case *ast.Ident:
		id = f
	case *ast.SelectorExpr:
		id = f.Sel
	}
	if id == nil {
		return 0, false
	}
	b, ok := e.info.Uses[id].(*types.Builtin)
	if !ok || (b.Name() != "Sizeof" && b.Name() != "Alignof") {
		return 0, false
	}
	arg, ok := e.info.Types[call.Args[0]]
	if !ok {
		return 0, false
	}
	t := e.subst(arg.Type)
	if hasTypeParams(t) {
		return 0, false
	}
	if b.Name() == "Sizeof" {
		return e.sizes.Sizeof(t), true
	}
	return e.sizes.Alignof(t), true
}

// gcLayout returns gc's size of t and whether t has pointers (as the
// runtime's _type.Pointers: whether its ptrdata is not 0), unless t
// involves type parameters.
func (e *encoder) gcLayout(t types.Type) (size int64, pointers bool, ok bool) {
	defer func() {
		if recover() != nil {
			ok = false
		}
	}()
	if hasTypeParams(t) {
		return 0, false, false
	}
	return e.sizes.Sizeof(t), hasPointers(t), true
}

// hasTypeParams reports whether t involves type parameters.
func hasTypeParams(t types.Type) bool {
	switch x := t.(type) {
	case *types.TypeParam:
		return true
	case *types.Named:
		if args := x.TypeArgs(); args != nil {
			for i := 0; i < args.Len(); i++ {
				if hasTypeParams(args.At(i)) {
					return true
				}
			}
		}
		return x.TypeParams().Len() > 0 && x.TypeArgs().Len() == 0
	case *types.Alias:
		return hasTypeParams(types.Unalias(x))
	case *types.Array:
		return hasTypeParams(x.Elem())
	case *types.Struct:
		for i := 0; i < x.NumFields(); i++ {
			if hasTypeParams(x.Field(i).Type()) {
				return true
			}
		}
	}
	return false
}

// hasPointers reports whether values of type t contain pointers for gc's
// garbage collector: pointers, strings, slices, maps, channels, functions
// and interfaces, and arrays (of positive length) and structs of these.
func hasPointers(t types.Type) bool {
	switch x := t.Underlying().(type) {
	case *types.Basic:
		return x.Kind() == types.String || x.Kind() == types.UnsafePointer
	case *types.Array:
		return x.Len() > 0 && hasPointers(x.Elem())
	case *types.Struct:
		for i := 0; i < x.NumFields(); i++ {
			if hasPointers(x.Field(i).Type()) {
				return true
			}
		}
		return false
	case *types.Pointer, *types.Slice, *types.Map, *types.Chan,
		*types.Signature, *types.Interface:
		return true
	}
	return false
}

// hostCompiler returns the compiler and version of the `go` command that
// provides the standard library (see ensureGoOnPath), run in dir, from
// `go version`, which prints `go version go1.27.3 linux/amd64` for gc and
// `go version go1.18 gccgo (GCC) 13.2.0 linux/amd64` for gccgo. It is
// empty if there is no `go` command.
func hostCompiler(dir string) map[string]any {
	cmd := exec.Command("go", "version")
	cmd.Dir = dir
	out, err := cmd.Output()
	fields := strings.Fields(string(out))
	if err != nil || len(fields) < 3 || fields[0] != "go" ||
		fields[1] != "version" {
		return map[string]any{"name": "", "version": ""}
	}
	name := "gc"
	if strings.Contains(string(out), "gccgo") {
		name = "gccgo"
	}
	version := fields[2]
	if version == "devel" && len(fields) > 3 {
		version = "devel " + fields[3]
	}
	return map[string]any{"name": name, "version": version}
}

// ensureGoOnPath makes the toolchain this helper was built with available to
// the gc importer (which runs `go list -export`) even if `go` is not on
// PATH at verification time.
//
// The helper is built with -trimpath, which leaves runtime.GOROOT() empty;
// then the GOROOT of the `go` on PATH, if any, is used. go/build (and so the
// gc importer) needs it to find the standard library.
func ensureGoOnPath() {
	goroot := runtime.GOROOT()
	if goroot == "" {
		if out, err := exec.Command("go", "env", "GOROOT").Output(); err == nil {
			goroot = strings.TrimSpace(string(out))
		}
	}
	if goroot == "" {
		return
	}
	build.Default.GOROOT = goroot
	if _, err := os.Stat(filepath.Join(goroot, "bin", "go")); err == nil {
		os.Setenv("PATH", filepath.Join(goroot, "bin")+
			string(os.PathListSeparator)+os.Getenv("PATH"))
	}
}

func main() {
	goarch := flag.String("goarch", runtime.GOARCH, "target GOARCH")
	out := flag.String("o", "", "output file (default: stdout)")
	noPrune := flag.Bool("no-prune", false,
		"keep the functions that the program cannot reach (see prune)")
	syntaxOnly := flag.Bool("syntax", false,
		"only check the syntax of the files (no output)")
	flag.Parse()
	if flag.NArg() == 0 {
		fmt.Fprintln(os.Stderr, "usage: cbmc-go-helper [-goarch ARCH] [-o OUT] FILE.go...")
		os.Exit(2)
	}

	// Go1.27 §Numeric_types, §Size_and_alignment_guarantees: sizes follow
	// gc on the target architecture, matching config.go in CBMC.
	sizes := types.SizesFor("gc", *goarch)
	if sizes == nil {
		fmt.Fprintf(os.Stderr, "unsupported GOARCH %q\n", *goarch)
		os.Exit(2)
	}
	os.Setenv("GOARCH", *goarch) // stdlib build constraints follow the target
	// The standard library's export data without the jsonv2 experiment,
	// whose encoding/json is the source of its model (see sourceModels).
	experiments := "nojsonv2"
	if prev := os.Getenv("GOEXPERIMENT"); prev != "" {
		experiments = prev + "," + experiments
	}
	os.Setenv("GOEXPERIMENT", experiments)
	ensureGoOnPath()

	fset := token.NewFileSet()
	enc := &encoder{fset: fset, sizes: sizes, fileIdx: map[string]int{},
		typeIdx: map[types.Type]int{}, objects: map[string]any{},
		ids: map[types.Object]string{}, byKey: map[string][]int{},
		structural: map[string][]int{},
		ctxt:       types.NewContext(), instances: map[string]*instance{},
		genericDecls: map[*types.Func]*ast.FuncDecl{},
		declFile:     map[*ast.FuncDecl]int{},
		localIDs:     map[localKey]string{},
		aliases:      map[types.Object]string{},
		selObj:       map[*ast.Ident]types.Object{},
		selPath:      map[*ast.SelectorExpr]selPath{},
		usedNames:    map[string]bool{}, reflectDerived: map[int]int{},
		replacedCache: map[types.Type]types.Type{}}
	var files []*ast.File
	var names []string
	failed := false
	for _, name := range flag.Args() {
		abs, err := filepath.Abs(name)
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(2)
		}
		f, err := parser.ParseFile(fset, abs, nil, parser.SkipObjectResolution)
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			failed = true
			continue
		}
		enc.fileIdx[abs] = len(names)
		names = append(names, abs)
		files = append(files, f)
	}
	if failed {
		os.Exit(3)
	}
	if *syntaxOnly {
		return
	}

	info := &types.Info{
		Types:      map[ast.Expr]types.TypeAndValue{},
		Instances:  map[*ast.Ident]types.Instance{},
		Defs:       map[*ast.Ident]types.Object{},
		Uses:       map[*ast.Ident]types.Object{},
		Implicits:  map[ast.Node]types.Object{},
		Selections: map[*ast.SelectorExpr]*types.Selection{},
	}
	enc.info = info
	conf := types.Config{
		GoVersion: "go1.27", // the pinned language version (plan §2.1)
		Context:   enc.ctxt,
		Sizes:     sizes,
		Error: func(err error) {
			fmt.Fprintln(os.Stderr, err)
			failed = true
		},
	}
	// The program's files are its package (as for go run); the user
	// packages they import are loaded from source.
	srcDir := filepath.Dir(names[0])
	modRoot, modPath := findModule(srcDir)
	ld := &loader{fset: fset, conf: &conf, info: info, enc: enc,
		names: &names, files: &files, modRoot: modRoot, modPath: modPath,
		srcDir: srcDir, byPath: map[string]*sourcePkg{},
		loading: map[string]bool{}, models: map[string]*types.Package{},
		real: map[string]*types.Package{}, failed: &failed}
	enc.loader = ld
	conf.Importer = &helperImporter{fset: fset, sizes: sizes,
		std: importer.ForCompiler(fset, "gc", nil), loader: ld}
	mainPkg := ld.check(files[0].Name.Name, append([]*ast.File(nil), files...))
	if failed {
		os.Exit(3)
	}
	pkg := mainPkg.pkg

	// The runtime package, which the conversion calls (models may have
	// imported it already).
	if _, err := ld.runtime(); err != nil || failed {
		fmt.Fprintln(os.Stderr, "runtime:", err)
		os.Exit(2)
	}

	// The predeclared basic types, which the front-end needs as dynamic
	// types of interface values even if the program does not name them.
	for k := types.Bool; k <= types.UnsafePointer; k++ {
		enc.typ(types.Typ[k])
	}
	// Generic declarations are recorded first: any file may instantiate
	// those of another.
	var filtered []*ast.File
	for i, f := range files {
		filtered = append(filtered, enc.withoutGenerics(f, i))
	}
	var asts []any
	for _, f := range filtered {
		asts = append(asts, enc.node(f))
	}
	// The packages, in the order of their initialisation, each with the
	// order of its variables' initialisation (types.Info.InitOrder).
	packages := []any{}
	for _, sp := range initOrder(ld.order) {
		initOrder := []any{}
		for _, init := range sp.initOrder {
			var lhs []string
			for _, v := range init.Lhs {
				lhs = append(lhs, enc.objID(v))
			}
			initOrder = append(initOrder, map[string]any{"lhs": lhs,
				"rhs": enc.node(init.Rhs)})
		}
		packages = append(packages, map[string]any{"path": sp.path,
			"init_order": initOrder})
	}
	// Instances, and the types' method sets, until no new instances arise:
	// encoding an instance or a method set may need further instances.
	for {
		enc.completeTypes()
		if len(enc.queue) == 0 {
			break
		}
		inst := enc.queue[0]
		enc.queue = enc.queue[1:]
		enc.cur = inst
		decl := enc.node(inst.decl)
		enc.cur = nil
		file := asts[enc.declFile[inst.decl]].(map[string]any)
		decls, _ := file["Decls"].([]any)
		file["Decls"] = append(decls, decl)
	}
	if !*noPrune {
		prune(asts, packages, enc.types, pkg.Path())
	}
	doc := map[string]any{"version": 1, "go_version": runtime.Version(),
		"goarch": *goarch, "package": pkg.Path(), "files": names,
		"asts": asts, "types": enc.types, "objects": enc.objects,
		"packages": packages, "compiler": hostCompiler(filepath.Dir(names[0])),
		"reflect_uses": enc.reflectUses()}

	w := os.Stdout
	if *out != "" {
		var err error
		if w, err = os.Create(*out); err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(2)
		}
		defer w.Close()
	}
	if err := json.NewEncoder(w).Encode(doc); err != nil {
		fmt.Fprintln(os.Stderr, strings.TrimSpace(err.Error()))
		os.Exit(2)
	}
}

// constantArray encodes an array or slice literal whose elements are
// constants of integer, boolean or floating-point types, or arrays or
// structs of such (e.g., generated tables), as the list of their values,
// "_cvals", without element nodes: its length in elements (the array's,
// or the slice's maximum index plus one), with zero values for missing
// elements. An element of array or struct type is the list of its
// elements or fields; a floating-point value is "b" and its IEEE 754 bits
// (see floatConstant). Nil if the literal is not such.
func (e *encoder) constantArray(c *ast.CompositeLit) map[string]any {
	const minElements = 16 // smaller literals are encoded as usual
	tv, ok := e.info.Types[c]
	if !ok || len(c.Elts) < minElements {
		return nil
	}
	t := e.subst(tv.Type)
	var elem types.Type
	length := int64(-1)
	switch u := t.Underlying().(type) {
	case *types.Array:
		elem, length = u.Elem(), u.Len()
	case *types.Slice:
		elem = u.Elem()
	default:
		return nil
	}
	if !constantEncodable(elem, 0) {
		return nil
	}
	values, n, ok := e.constantElements(c, elem)
	if !ok {
		return nil
	}
	if length < 0 {
		length = n
	}
	cvals := make([]any, length)
	for i := range cvals {
		if v, ok := values[int64(i)]; ok {
			cvals[i] = v
		} else {
			cvals[i] = constantZero(elem)
		}
	}
	out := map[string]any{"_k": "CompositeLit", "_t": e.typ(t),
		"_cvals": cvals}
	if p := e.fset.Position(c.Pos()); p.IsValid() {
		out["_p"] = []int{e.fileIdx[p.Filename], p.Line, p.Column}
	}
	return out
}

// constantEncodable reports whether values of type t can be sent as
// constants by constantArray: integers, booleans, floating-point numbers,
// and arrays and structs of those (to a small depth).
func constantEncodable(t types.Type, depth int) bool {
	if depth > 3 {
		return false
	}
	switch u := t.Underlying().(type) {
	case *types.Basic:
		return u.Info()&(types.IsInteger|types.IsBoolean|types.IsFloat) != 0 &&
			u.Info()&types.IsUntyped == 0
	case *types.Array:
		return u.Len() <= 64 && constantEncodable(u.Elem(), depth+1)
	case *types.Struct:
		for i := 0; i < u.NumFields(); i++ {
			if !constantEncodable(u.Field(i).Type(), depth+1) {
				return false
			}
		}
		return true
	}
	return false
}

// constantZero is the encoding of the zero value of t (see constantArray).
func constantZero(t types.Type) any {
	switch u := t.Underlying().(type) {
	case *types.Basic:
		switch {
		case u.Info()&types.IsBoolean != 0:
			return "false"
		case u.Info()&types.IsFloat != 0:
			return "b0"
		}
		return "0"
	case *types.Array:
		list := make([]any, u.Len())
		for i := range list {
			list[i] = constantZero(u.Elem())
		}
		return list
	case *types.Struct:
		list := make([]any, u.NumFields())
		for i := range list {
			list[i] = constantZero(u.Field(i).Type())
		}
		return list
	}
	return nil
}

// constantElements returns the encoded elements of the array or slice
// literal c with elements of type elem, by index, and the number of
// elements (the maximum index plus one).
func (e *encoder) constantElements(c *ast.CompositeLit, elem types.Type) (map[int64]any, int64, bool) {
	values := map[int64]any{}
	next, maxIndex := int64(0), int64(-1)
	for _, el := range c.Elts {
		v := el
		if kv, ok := el.(*ast.KeyValueExpr); ok {
			kt, ok := e.info.Types[kv.Key]
			if !ok || kt.Value == nil {
				return nil, 0, false
			}
			i, ok := constant.Int64Val(constant.ToInt(kt.Value))
			if !ok {
				return nil, 0, false
			}
			next, v = i, kv.Value
		}
		value, ok := e.constantValue(v, elem)
		if !ok {
			return nil, 0, false
		}
		values[next] = value
		maxIndex = max(maxIndex, next)
		next++
	}
	return values, maxIndex + 1, true
}

// constantValue encodes expression x of type t (see constantArray).
func (e *encoder) constantValue(x ast.Expr, t types.Type) (any, bool) {
	x = ast.Unparen(x)
	switch u := t.Underlying().(type) {
	case *types.Basic:
		tv, ok := e.info.Types[x]
		if !ok || tv.Value == nil {
			return nil, false
		}
		switch {
		case u.Info()&types.IsBoolean != 0:
			return strconv.FormatBool(constant.BoolVal(tv.Value)), true
		case u.Info()&types.IsFloat != 0:
			if u.Kind() == types.Float32 {
				f, _ := constant.Float32Val(constant.ToFloat(tv.Value))
				return "b" + strconv.FormatUint(uint64(math.Float32bits(f)), 10), true
			}
			f, _ := constant.Float64Val(constant.ToFloat(tv.Value))
			return "b" + strconv.FormatUint(math.Float64bits(f), 10), true
		}
		return constant.ToInt(tv.Value).ExactString(), true
	case *types.Array:
		lit, ok := x.(*ast.CompositeLit)
		if !ok {
			return nil, false
		}
		values, _, ok := e.constantElements(lit, u.Elem())
		if !ok {
			return nil, false
		}
		list := make([]any, u.Len())
		for i := range list {
			if v, ok := values[int64(i)]; ok {
				list[i] = v
			} else {
				list[i] = constantZero(u.Elem())
			}
		}
		return list, true
	case *types.Struct:
		lit, ok := x.(*ast.CompositeLit)
		if !ok {
			return nil, false
		}
		list := make([]any, u.NumFields())
		for i := range list {
			list[i] = constantZero(u.Field(i).Type())
		}
		for i, el := range lit.Elts {
			field, v := i, el
			if kv, ok := el.(*ast.KeyValueExpr); ok {
				id, ok := kv.Key.(*ast.Ident)
				if !ok {
					return nil, false
				}
				field = -1
				for j := 0; j < u.NumFields(); j++ {
					if u.Field(j).Name() == id.Name {
						field = j
					}
				}
				if field < 0 {
					return nil, false
				}
				v = kv.Value
			}
			if field >= u.NumFields() {
				return nil, false
			}
			value, ok := e.constantValue(v, u.Field(field).Type())
			if !ok {
				return nil, false
			}
			list[field] = value
		}
		return list, true
	}
	return nil, false
}
