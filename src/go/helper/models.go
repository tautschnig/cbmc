// Models of standard-library packages.

package main

import (
	"embed"
	"go/ast"
	"go/parser"
	"go/types"
	"io/fs"
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

// loadModel loads the model of standard-library package path, if any.
func (l *loader) loadModel(path string) error {
	if _, done := l.models[path]; done {
		return nil
	}
	l.models[path] = nil
	dir := "models/" + path
	entries, err := fs.ReadDir(modelFS, dir)
	if err != nil {
		return nil // no model
	}
	var files []*ast.File
	for _, e := range entries {
		if !strings.HasSuffix(e.Name(), ".go") {
			continue
		}
		src, err := fs.ReadFile(modelFS, dir+"/"+e.Name())
		if err != nil {
			return err
		}
		name := modelPrefix + path + "/" + e.Name()
		f, err := parser.ParseFile(l.fset, name, src, parser.SkipObjectResolution)
		if err != nil {
			return err
		}
		l.addFile(name, f)
		files = append(files, f)
	}
	l.models[path] = l.check(modelPrefix+path, files).pkg
	return nil
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
