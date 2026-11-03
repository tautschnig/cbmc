// Models of standard-library packages.

package main

import (
	"embed"
	"go/ast"
	"go/build"
	"go/parser"
	"go/token"
	"go/types"
	"io/fs"
	pathpkg "path"
	"path/filepath"
	"strings"
)

// Models of standard-library functions (Go1.27 does not specify the
// library; the front-end cannot convert its real implementation, which
// uses unsafe and assembly). models/<path>/*.go is the package
// "cprover/std/<path>"; a call of function F of package <path> denotes the
// model's F if it exists with an identical signature. The real package
// (from the toolchain's export data) still provides the declarations, so
// that programs type-check exactly as with gc; calls of functions without a
// model are reported by the front-end.
//
//go:embed models
var modelFS embed.FS

const modelPrefix = "cprover/std/"

// Standard-library packages that their models replace: the program is
// type-checked against models/<path> as package <path>, so that only the
// API the model declares is available. Their types have methods that
// operate on state the real types keep in internal fields, which models of
// functions alone (see model) cannot reach.
var replaced = map[string]bool{"sync": true, "sync/atomic": true}

// loadReplacement loads the model that replaces standard-library package
// path (see replaced).
func (l *loader) loadReplacement(path string) (*types.Package, error) {
	if sp, ok := l.byPath[path]; ok {
		return sp.pkg, nil
	}
	files, err := l.parseModel(path)
	if err != nil {
		return nil, err
	}
	return l.check(path, files).pkg, nil
}

// loadModel loads the model of standard-library package path, if any.
func (l *loader) loadModel(path string) error {
	if _, done := l.models[path]; done {
		return nil
	}
	l.models[path] = nil
	files, err := l.parseModel(path)
	if err != nil || files == nil {
		return err
	}
	l.models[path] = l.check(modelPrefix+path, files).pkg
	return nil
}

// Standard-library packages whose model is their own source in GOROOT:
// plain Go, without assembly, linkname'd variables or state that the
// front-end cannot model. Each maps to the files to use (all that go build
// selects for the target if nil) and the build tags to select them with.
// The source is type-checked as package "cprover/std/<path>", like other
// models, so that the real package's types (from export data) are not
// affected.
//
// With overrides, the functions of models/<path> replace those of the same
// names in the source (and imports that no longer have uses are dropped).
var sourceModels = map[string]struct {
	files     []string
	tags      []string
	overrides bool
}{
	"cmp":                {},
	"internal/byteorder": {},
	// The bootstrap variant of the overflow and divide errors, rather than
	// the runtime's, which bits_errors.go links to.
	"math/bits": {tags: []string{"compiler_bootstrap"}},
	// The portable implementations (as for architectures without assembly):
	// MaxLen stays 0, so that callers never use Index and IndexString.
	"internal/bytealg": {files: []string{"bytealg.go", "compare_generic.go",
		"count_generic.go", "equal_generic.go", "index_generic.go",
		"indexbyte_generic.go", "lastindexbyte_generic.go"}},
	// overlaps compares addresses through package unsafe.
	"slices": {overrides: true},
	// The conversions between floating-point numbers and their bits use
	// package unsafe.
	"internal/strconv": {overrides: true},
	// Clone converts with unsafe.String.
	"internal/stringslite": {overrides: true},
	// The integer formatting of the model is simpler than
	// internal/strconv's, which uses tables.
	"strconv": {overrides: true},
	// Packages whose functions only mention types of other packages (the
	// model's own types are not identical to the real package's).
	"encoding/hex":  {},
	"sort":          {},
	"hash/fnv":      {},
	"unicode/utf16": {},
}

// parseSourceModel parses the files of the source model of path (see
// sourceModels).
func (l *loader) parseSourceModel(path string) ([]*ast.File, error) {
	sm := sourceModels[path]
	ctxt := build.Default
	ctxt.BuildTags = append(append([]string(nil), ctxt.BuildTags...),
		sm.tags...)
	bp, err := ctxt.Import(path, "", 0)
	if err != nil {
		return nil, err
	}
	names := sm.files
	if names == nil {
		names = bp.GoFiles
	}
	var overrides []*ast.File
	replaced := map[string]bool{}
	if sm.overrides {
		if overrides, err = l.parseEmbeddedModel(path); err != nil {
			return nil, err
		}
		for _, f := range overrides {
			for _, d := range f.Decls {
				if fd, ok := d.(*ast.FuncDecl); ok && fd.Recv == nil {
					replaced[fd.Name.Name] = true
				}
			}
		}
	}
	var files []*ast.File
	for _, name := range names {
		abs := filepath.Join(bp.Dir, name)
		f, err := parser.ParseFile(l.fset, abs, nil, parser.SkipObjectResolution)
		if err != nil {
			return nil, err
		}
		if len(replaced) > 0 {
			override(f, replaced)
		}
		l.addFile(abs, f)
		files = append(files, f)
	}
	return append(files, overrides...), nil
}

// override removes the functions named in replaced from f, and the
// imports that then have no uses.
func override(f *ast.File, replaced map[string]bool) {
	var decls []ast.Decl
	for _, d := range f.Decls {
		if fd, ok := d.(*ast.FuncDecl); ok && fd.Recv == nil &&
			replaced[fd.Name.Name] {
			continue
		}
		decls = append(decls, d)
	}
	f.Decls = decls
	used := map[string]bool{}
	ast.Inspect(f, func(n ast.Node) bool {
		if sel, ok := n.(*ast.SelectorExpr); ok {
			if id, ok := sel.X.(*ast.Ident); ok {
				used[id.Name] = true
			}
		}
		return true
	})
	for _, d := range f.Decls {
		gd, ok := d.(*ast.GenDecl)
		if !ok || gd.Tok != token.IMPORT {
			continue
		}
		var specs []ast.Spec
		for _, spec := range gd.Specs {
			is := spec.(*ast.ImportSpec)
			name := pathpkg.Base(strings.Trim(is.Path.Value, `"`))
			if is.Name != nil {
				name = is.Name.Name
			}
			if name == "_" || name == "." || used[name] {
				specs = append(specs, spec)
			}
		}
		gd.Specs = specs
	}
}

// parseModel parses the files of models/<path>, or of the source model of
// path (see sourceModels); none if there is no model.
func (l *loader) parseModel(path string) ([]*ast.File, error) {
	if _, ok := sourceModels[path]; ok {
		return l.parseSourceModel(path)
	}
	return l.parseEmbeddedModel(path)
}

// parseEmbeddedModel parses the files of models/<path>; none if there are
// none.
func (l *loader) parseEmbeddedModel(path string) ([]*ast.File, error) {
	dir := "models/" + path
	entries, err := fs.ReadDir(modelFS, dir)
	if err != nil {
		return nil, nil // no model
	}
	var files []*ast.File
	for _, e := range entries {
		if !strings.HasSuffix(e.Name(), ".go") {
			continue
		}
		src, err := fs.ReadFile(modelFS, dir+"/"+e.Name())
		if err != nil {
			return nil, err
		}
		name := modelPrefix + path + "/" + e.Name()
		f, err := parser.ParseFile(l.fset, name, src, parser.SkipObjectResolution)
		if err != nil {
			return nil, err
		}
		l.addFile(name, f)
		files = append(files, f)
	}
	return files, nil
}

// model returns the model of standard-library function f, or nil.
func (l *loader) model(f *types.Func) *types.Func {
	if f.Pkg() == nil || isMethod(f) {
		return nil
	}
	m := l.models[f.Pkg().Path()]
	if m == nil {
		return nil
	}
	mf, _ := m.Scope().Lookup(f.Name()).(*types.Func)
	if mf == nil || !types.Identical(mf.Type(), f.Type()) {
		return nil
	}
	return mf
}

// realType returns the named type of the real standard-library package
// that a named type of its source model (see sourceModels) stands for, or
// t. The model's values then have the real types, as gc's do, so that type
// assertions and switches of the program (whose types are the real ones)
// see them; the methods of the real types are the model's (see
// modelMethod). The two types have the same declaration, from the same
// source.
func (e *encoder) realType(t types.Type) types.Type {
	if e.loader == nil {
		return t
	}
	// Types composed of model types are composed of the real ones, so that
	// they are identical to the program's.
	switch x := t.(type) {
	case *types.Pointer:
		if el := e.realType(x.Elem()); el != x.Elem() {
			return types.NewPointer(el)
		}
		return t
	case *types.Slice:
		if el := e.realType(x.Elem()); el != x.Elem() {
			return types.NewSlice(el)
		}
		return t
	case *types.Array:
		if el := e.realType(x.Elem()); el != x.Elem() {
			return types.NewArray(el, x.Len())
		}
		return t
	case *types.Map:
		k, v := e.realType(x.Key()), e.realType(x.Elem())
		if k != x.Key() || v != x.Elem() {
			return types.NewMap(k, v)
		}
		return t
	case *types.Chan:
		if el := e.realType(x.Elem()); el != x.Elem() {
			return types.NewChan(x.Dir(), el)
		}
		return t
	case *types.Signature:
		if x.TypeParams().Len() > 0 || x.Recv() != nil {
			return t
		}
		params, pc := e.realTuple(x.Params())
		results, rc := e.realTuple(x.Results())
		if pc || rc {
			return types.NewSignatureType(nil, nil, nil, params, results,
				x.Variadic())
		}
		return t
	}
	n, ok := t.(*types.Named)
	if !ok || n.Obj().Pkg() == nil || n.TypeParams().Len() > 0 {
		return t
	}
	path, ok := strings.CutPrefix(n.Obj().Pkg().Path(), modelPrefix)
	if !ok {
		return t
	}
	if _, isSource := sourceModels[path]; !isSource {
		return t
	}
	real := e.loader.real[path]
	if real == nil {
		return t
	}
	tn, _ := real.Scope().Lookup(n.Obj().Name()).(*types.TypeName)
	if tn == nil || tn.IsAlias() {
		return t
	}
	return tn.Type()
}

// realTuple maps the types of tup with realType, reporting whether any
// changed.
func (e *encoder) realTuple(tup *types.Tuple) (*types.Tuple, bool) {
	changed := false
	var vars []*types.Var
	for i := 0; i < tup.Len(); i++ {
		v := tup.At(i)
		t := e.realType(v.Type())
		if t != v.Type() {
			changed = true
			v = types.NewParam(v.Pos(), v.Pkg(), v.Name(), t)
		}
		vars = append(vars, v)
	}
	if !changed {
		return tup, false
	}
	return types.NewTuple(vars...), true
}

// modelMethod returns the method of the source model's type (see
// realType) that method f of a real standard-library type stands for, or
// nil.
func (l *loader) modelMethod(f *types.Func) *types.Func {
	if !isMethod(f) || f.Pkg() == nil {
		return nil
	}
	path := f.Pkg().Path()
	if _, isSource := sourceModels[path]; !isSource || l.models[path] == nil {
		return nil
	}
	recv := f.Signature().Recv().Type()
	if p, ok := recv.(*types.Pointer); ok {
		recv = p.Elem()
	}
	n, ok := recv.(*types.Named)
	if !ok || n.TypeParams().Len() > 0 {
		return nil
	}
	tn, _ := l.models[path].Scope().Lookup(n.Obj().Name()).(*types.TypeName)
	if tn == nil {
		return nil
	}
	m, _, _ := types.LookupFieldOrMethod(tn.Type(), true, tn.Pkg(), f.Name())
	mf, _ := m.(*types.Func)
	if mf == nil || mf.Pkg() != tn.Pkg() {
		return nil
	}
	return mf
}

// modelVar returns the model of package-level variable v of the standard
// library, a variable of the same name and an identical type in the
// package's model, or nil.
func (l *loader) modelVar(v *types.Var) *types.Var {
	if v.Pkg() == nil || v.Parent() != v.Pkg().Scope() {
		return nil
	}
	m := l.models[v.Pkg().Path()]
	if m == nil {
		return nil
	}
	mv, _ := m.Scope().Lookup(v.Name()).(*types.Var)
	if mv == nil || !types.Identical(mv.Type(), v.Type()) {
		return nil
	}
	return mv
}

// genericModel returns the model of generic standard-library function f,
// if its instance is identical to inst.
func (e *encoder) genericModel(f *types.Func, inst types.Instance) *types.Func {
	if f == nil || e.loader == nil || isMethod(f) || f.Pkg() == nil {
		return nil
	}
	m := e.loader.models[f.Pkg().Path()]
	if m == nil {
		return nil
	}
	mf, _ := m.Scope().Lookup(f.Name()).(*types.Func)
	if mf == nil || e.genericDecls[mf] == nil {
		return nil
	}
	t, err := types.Instantiate(e.ctxt, mf.Type(), typeList(inst.TypeArgs),
		true)
	if err != nil || !types.Identical(t, inst.Type) {
		return nil
	}
	return mf
}
