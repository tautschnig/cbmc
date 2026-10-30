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
// Exit status: 0 on success, 2 on usage or I/O errors, 3 on syntax or type
// errors (reported on stderr as "file:line:col: message").
//
// Output schema (version 1):
//
//	{ "version": 1, "go_version": "go1.27.3", "goarch": "amd64",
//	  "package": "main", "files": ["/abs/main.go", ...],
//	  "asts": [ <File node>, ... ],
//	  "types": [ <type>, ... ],          // referenced by index
//	  "objects": { "<id>": <object> },   // referenced by id
//	  "init_order": [ {"lhs": ["<id>", ...], "rhs": <expr node>}, ... ] }
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
//	"_inst" type-argument indices for instantiated generic identifiers
//	"_impl" id of an implicitly declared object (e.g. per-clause type-switch
//	        variables, types.Info.Implicits)
//	"_sb"   for string constants, their bytes
//	"_fb"   for constants of floating-point type, the IEEE 754 bits of the
//	        value rounded to the type; "_fbr"/"_fbi" for complex types
//
// The last entry of "files" and "asts" is the front-end's runtime package
// "cprover/rt" (runtime/rt.go), whose objects have ids "cprover/rt.<name>".
package main

import (
	_ "embed"
	"encoding/json"
	"flag"
	"fmt"
	"go/ast"
	"go/constant"
	"go/importer"
	"go/parser"
	"go/token"
	"go/types"
	"math"
	"os"
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

// importer that serves "cprover" from the embedded source and everything
// else from the gc toolchain's export data.
type helperImporter struct {
	fset    *token.FileSet
	sizes   types.Sizes
	std     types.Importer
	cprover *types.Package
}

func (h *helperImporter) Import(path string) (*types.Package, error) {
	if path != cproverPath {
		return h.std.Import(path)
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
	fset    *token.FileSet
	info    *types.Info
	fileIdx map[string]int
	types   []any
	typeIdx map[types.Type]int
	objects map[string]any
	ids     map[types.Object]string
}

// objID gives every object a unique id. Package-level objects and methods
// get readable ids ("main.f", "main.T.M"); objects in local scopes get a
// numeric suffix ("main.x#3") because shadowing makes names ambiguous.
func (e *encoder) objID(o types.Object) string {
	if id, ok := e.ids[o]; ok {
		return id
	}
	var id string
	pkg := ""
	if o.Pkg() != nil {
		pkg = o.Pkg().Path()
	}
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
	case *types.Const:
		entry["kind"] = "const"
		entry["value"] = x.Val().ExactString()
	case *types.TypeName:
		entry["kind"] = "type"
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
		entry["type"] = e.typ(o.Type())
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
	t = types.Unalias(t)
	if i, ok := e.typeIdx[t]; ok {
		return i
	}
	i := len(e.types)
	e.typeIdx[t] = i
	e.types = append(e.types, nil)
	var entry map[string]any
	switch x := t.(type) {
	case *types.Basic:
		entry = map[string]any{"k": "basic", "kind": basicNames[x.Kind()]}
	case *types.Named:
		entry = map[string]any{"k": "named", "obj": e.objID(x.Obj()),
			"under": e.typ(x.Underlying())}
		if args := x.TypeArgs(); args.Len() > 0 {
			var targs []int
			for j := 0; j < args.Len(); j++ {
				targs = append(targs, e.typ(args.At(j)))
			}
			entry["targs"] = targs
		}
	case *types.Pointer:
		entry = map[string]any{"k": "pointer", "elem": e.typ(x.Elem())}
	case *types.Slice:
		entry = map[string]any{"k": "slice", "elem": e.typ(x.Elem())}
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
			fields = append(fields, map[string]any{"name": f.Name(),
				"type": e.typ(f.Type()), "embedded": f.Embedded(),
				"obj": e.objID(f)})
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
	v := reflect.ValueOf(n).Elem()
	out := map[string]any{"_k": v.Type().Name()}
	if p := e.fset.Position(n.Pos()); p.IsValid() {
		out["_p"] = []int{e.fileIdx[p.Filename], p.Line, p.Column}
	}
	for i := 0; i < v.NumField(); i++ {
		f, name := v.Field(i), v.Type().Field(i).Name
		switch name {
		case "Obj", "Scope", "Unresolved", "Imports", "Comments", "Doc",
			"Comment":
			continue // superseded by go/types, or irrelevant
		}
		if r := e.value(f); r != nil {
			out[name] = r
		}
	}
	if x, ok := n.(ast.Expr); ok {
		if tv, ok := e.info.Types[x]; ok {
			out["_t"] = e.typ(tv.Type)
			if tv.IsType() {
				out["_tx"] = true
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
		if o := e.info.Defs[id]; o != nil {
			out["_obj"] = e.objID(o)
			out["_def"] = true
		} else if o := e.info.Uses[id]; o != nil {
			out["_obj"] = e.objID(o)
		}
		if inst, ok := e.info.Instances[id]; ok {
			var targs []int
			for i := 0; i < inst.TypeArgs.Len(); i++ {
				targs = append(targs, e.typ(inst.TypeArgs.At(i)))
			}
			out["_inst"] = targs
			out["_t"] = e.typ(inst.Type)
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
	if se, ok := n.(*ast.SelectorExpr); ok {
		if s := e.info.Selections[se]; s != nil {
			out["_sel"] = map[string]any{
				"kind":     [...]string{"field", "method", "methodexpr"}[s.Kind()],
				"path":     s.Index(),
				"indirect": s.Indirect(),
				"obj":      e.objID(s.Obj()),
			}
		}
	}
	return out
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

// ensureGoOnPath makes the toolchain this helper was built with available to
// the gc importer (which runs `go list -export`) even if `go` is not on
// PATH at verification time.
func ensureGoOnPath() {
	if _, err := os.Stat(filepath.Join(runtime.GOROOT(), "bin", "go")); err == nil {
		os.Setenv("PATH", filepath.Join(runtime.GOROOT(), "bin")+
			string(os.PathListSeparator)+os.Getenv("PATH"))
	}
}

func main() {
	goarch := flag.String("goarch", runtime.GOARCH, "target GOARCH")
	out := flag.String("o", "", "output file (default: stdout)")
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
	ensureGoOnPath()

	fset := token.NewFileSet()
	enc := &encoder{fset: fset, fileIdx: map[string]int{},
		typeIdx: map[types.Type]int{}, objects: map[string]any{},
		ids: map[types.Object]string{}}
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
		Sizes:     sizes,
		Importer: &helperImporter{fset: fset, sizes: sizes,
			std: importer.ForCompiler(fset, "gc", nil)},
		Error: func(err error) {
			fmt.Fprintln(os.Stderr, err)
			failed = true
		},
	}
	pkg, _ := conf.Check(files[0].Name.Name, fset, files, info)
	if failed {
		os.Exit(3)
	}
	// The program's initialisation order, copied before checking the runtime
	// package (which shares info, and so reuses InitOrder's array).
	programInitOrder := append([]*types.Initializer(nil), info.InitOrder...)

	// The runtime package, with its own file index after the program's.
	rtFile, err := parser.ParseFile(fset, "cprover/rt/rt.go", runtimeSource,
		parser.SkipObjectResolution)
	if err != nil {
		fmt.Fprintln(os.Stderr, "runtime:", err)
		os.Exit(2)
	}
	enc.fileIdx["cprover/rt/rt.go"] = len(names)
	names = append(names, "cprover/rt/rt.go")
	files = append(files, rtFile)
	if _, err := conf.Check(runtimePath, fset, []*ast.File{rtFile}, info); err != nil || failed {
		fmt.Fprintln(os.Stderr, "runtime:", err)
		os.Exit(2)
	}

	var asts []any
	for _, f := range files {
		asts = append(asts, enc.node(f))
	}
	initOrder := []any{}
	for _, init := range programInitOrder {
		var lhs []string
		for _, v := range init.Lhs {
			lhs = append(lhs, enc.objID(v))
		}
		initOrder = append(initOrder, map[string]any{"lhs": lhs,
			"rhs": enc.node(init.Rhs)})
	}
	doc := map[string]any{"version": 1, "go_version": runtime.Version(),
		"goarch": *goarch, "package": pkg.Path(), "files": names,
		"asts": asts, "types": enc.types, "objects": enc.objects,
		"init_order": initOrder}

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
