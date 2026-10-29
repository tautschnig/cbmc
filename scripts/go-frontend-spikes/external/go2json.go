// Spike A: external parser/type-checker for the CBMC Go frontend.
//
// Parses and type-checks one Go package with go/parser and go/types, and
// emits the syntax tree as JSON, annotated with type-checker results:
//
//   "_k"     node kind (Go AST type name)
//   "_pos"   "line:col"
//   "_t"     type of an expression (types.Info.Types / TypeOf)
//   "_cv"    exact constant value (go/constant, arbitrary precision)
//   "_obj"   for identifiers: the object they define or use, with a
//            package-qualified unique id (resolves scopes, shadowing,
//            imports, and embedded-field promotion)
//   "_sel"   for selectors: field/method selection, embedding path,
//            and whether an implicit dereference happens
//   "_inst"  for generic uses: the inferred type arguments
//
// Usage: go2json [-importer source|gc] DIR
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"go/ast"
	"go/constant"
	"go/importer"
	"go/parser"
	"go/token"
	"go/types"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
)

type encoder struct {
	fset *token.FileSet
	info *types.Info
	ids  map[types.Object]string
}

// objID gives every object a stable, unique, package-qualified name so the
// C++ side never needs to redo scope resolution.
func (e *encoder) objID(o types.Object) string {
	if id, ok := e.ids[o]; ok {
		return id
	}
	pkg := "_"
	if o.Pkg() != nil {
		pkg = o.Pkg().Path()
	}
	id := fmt.Sprintf("%s.%s#%d", pkg, o.Name(), len(e.ids))
	if o.Parent() == o.Pkg().Scope() || o.Parent() == types.Universe {
		id = pkg + "." + o.Name() // package-level: keep readable
	}
	e.ids[o] = id
	return id
}

func objKind(o types.Object) string {
	switch o.(type) {
	case *types.Var:
		if o.(*types.Var).IsField() {
			return "field"
		}
		return "var"
	case *types.Func:
		return "func"
	case *types.Const:
		return "const"
	case *types.TypeName:
		return "type"
	case *types.PkgName:
		return "package"
	case *types.Builtin:
		return "builtin"
	case *types.Nil:
		return "nil"
	}
	return "other"
}

func (e *encoder) node(n ast.Node) any {
	if n == nil || reflect.ValueOf(n).IsNil() {
		return nil
	}
	v := reflect.ValueOf(n).Elem()
	out := map[string]any{"_k": v.Type().Name()}
	if p := e.fset.Position(n.Pos()); p.IsValid() {
		out["_pos"] = fmt.Sprintf("%d:%d", p.Line, p.Column)
	}
	for i := 0; i < v.NumField(); i++ {
		f, name := v.Field(i), v.Type().Field(i).Name
		if name == "Obj" || name == "Scope" || name == "Unresolved" ||
			name == "Imports" || name == "Comments" || name == "Doc" ||
			name == "Comment" {
			continue // superseded by go/types or irrelevant
		}
		if r := e.value(f); r != nil {
			out[name] = r
		}
	}
	if x, ok := n.(ast.Expr); ok {
		if tv, ok := e.info.Types[x]; ok {
			out["_t"] = tv.Type.String()
			if tv.Value != nil {
				out["_cv"] = tv.Value.ExactString()
				if tv.Value.Kind() == constant.Int {
					out["_cv_kind"] = "int"
				}
			}
		}
	}
	if id, ok := n.(*ast.Ident); ok {
		if o := e.info.Defs[id]; o != nil {
			out["_obj"] = map[string]any{"def": true, "id": e.objID(o),
				"kind": objKind(o), "type": o.Type().String()}
			if c, ok := o.(*types.Const); ok {
				out["_cv"] = c.Val().ExactString()
			}
		} else if o := e.info.Uses[id]; o != nil {
			out["_obj"] = map[string]any{"id": e.objID(o),
				"kind": objKind(o), "type": o.Type().String()}
		}
		if inst, ok := e.info.Instances[id]; ok {
			var targs []string
			for i := 0; i < inst.TypeArgs.Len(); i++ {
				targs = append(targs, inst.TypeArgs.At(i).String())
			}
			out["_inst"] = targs
		}
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
		var xs []any
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
		if f.Type() == reflect.TypeOf(token.ILLEGAL) {
			return token.Token(f.Int()).String()
		}
		if f.Type() == reflect.TypeOf(ast.SEND) {
			return int(f.Int())
		}
	}
	return nil
}

func main() {
	imp := flag.String("importer", "gc", "gc (export data) or source")
	flag.Parse()
	dir := flag.Arg(0)

	fset := token.NewFileSet()
	matches, _ := filepath.Glob(filepath.Join(dir, "*.go"))
	sort.Strings(matches)
	var files []*ast.File
	for _, m := range matches {
		if strings.HasSuffix(m, "_test.go") {
			continue
		}
		f, err := parser.ParseFile(fset, m, nil, parser.SkipObjectResolution)
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			os.Exit(2)
		}
		files = append(files, f)
	}
	info := &types.Info{
		Types:      map[ast.Expr]types.TypeAndValue{},
		Defs:       map[*ast.Ident]types.Object{},
		Uses:       map[*ast.Ident]types.Object{},
		Selections: map[*ast.SelectorExpr]*types.Selection{},
		Instances:  map[*ast.Ident]types.Instance{},
		Implicits:  map[ast.Node]types.Object{},
	}
	conf := types.Config{Importer: importer.ForCompiler(fset, *imp, nil)}
	if _, err := conf.Check(files[0].Name.Name, fset, files, info); err != nil {
		fmt.Fprintln(os.Stderr, err) // type errors are fatal for CBMC
		os.Exit(3)
	}
	enc := &encoder{fset: fset, info: info, ids: map[types.Object]string{}}
	var out []any
	for _, f := range files {
		out = append(out, enc.node(f))
	}
	w := json.NewEncoder(os.Stdout)
	if err := w.Encode(out); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
