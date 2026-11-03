// Models of standard-library packages.

package main

import (
	"embed"
	"go/ast"
	"go/build"
	"go/constant"
	"go/parser"
	"go/token"
	"go/types"
	"io/fs"
	pathpkg "path"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
	"unicode/utf8"
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
	// MaxLen stays 0, so that callers never use Index and IndexString. The
	// runtime implements MakeNoZero.
	"internal/bytealg": {files: []string{"bytealg.go", "compare_generic.go",
		"count_generic.go", "equal_generic.go", "index_generic.go",
		"indexbyte_generic.go", "lastindexbyte_generic.go"},
		overrides: true},
	// overlaps compares addresses through package unsafe.
	"slices": {overrides: true},
	// The conversions between floating-point numbers and their bits use
	// package unsafe.
	"internal/strconv": {overrides: true},
	// Clone converts with unsafe.String.
	"internal/stringslite": {overrides: true},
	// The searches of models/strings are simpler loops.
	"strings": {overrides: true},
	"bytes":   {},
	// Decoding and encoding use the runtime's (cprover/rt).
	"unicode/utf8": {overrides: true},
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
	decls = nil
	for _, d := range f.Decls {
		gd, ok := d.(*ast.GenDecl)
		if !ok || gd.Tok != token.IMPORT {
			decls = append(decls, d)
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
		// A declaration without imports left is dropped (go/ast cannot
		// give the end of an empty one without parentheses).
		if len(specs) > 0 {
			gd.Specs = specs
			decls = append(decls, gd)
		}
	}
	f.Decls = decls
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
	// The signature of a source model's function mentions the model's own
	// types, which stand for the real ones (see realType).
	if mf == nil || !types.Identical(l.enc.realSignature(mf.Type()), f.Type()) {
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
	}
	n, ok := t.(*types.Named)
	if !ok || n.Obj().Pkg() == nil {
		return t
	}
	if r := e.replacementType(n); r != nil {
		return r
	}
	if n.TypeParams().Len() > 0 {
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

// replacementType returns the type of the model of a replaced package (see
// replaced) that named type n of the real package stands for, or nil. The
// real package is that of the standard-library packages loaded from
// export data, whose types (e.g., the fields of a struct) may refer to its
// types; those must be the model's, which has the same path and so the
// same object ids. A type that the model does not declare stays the real
// one.
func (e *encoder) replacementType(n *types.Named) types.Type {
	path := n.Obj().Pkg().Path()
	if !replaced[path] {
		return nil
	}
	sp := e.loader.byPath[path]
	if sp == nil || sp.pkg == n.Obj().Pkg() {
		return nil
	}
	tn, _ := sp.pkg.Scope().Lookup(n.Obj().Name()).(*types.TypeName)
	if tn == nil {
		return nil
	}
	args := n.TypeArgs()
	if args.Len() == 0 {
		return tn.Type()
	}
	var targs []types.Type
	for i := 0; i < args.Len(); i++ {
		targs = append(targs, e.realType(args.At(i)))
	}
	inst, err := types.Instantiate(e.ctxt, tn.Type(), targs, false)
	if err != nil {
		return nil
	}
	return inst
}

// realTuple maps the types of tup with realType, reporting whether any
// changed.
//
// Signatures are not mapped (their parameters are the objects that the
// function bodies refer to), except by realSignature.
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

// realSignature is realType for the signature of a function (see model):
// its parameters' types mapped.
func (e *encoder) realSignature(t types.Type) types.Type {
	x, ok := t.(*types.Signature)
	if !ok || x.TypeParams().Len() > 0 || x.Recv() != nil {
		return t
	}
	params, pc := e.realTuple(x.Params())
	results, rc := e.realTuple(x.Results())
	if !pc && !rc {
		return t
	}
	return types.NewSignatureType(nil, nil, nil, params, results, x.Variadic())
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

// errorfWraps returns, for a call of fmt.Errorf with the constant format
// and nargs operands, the operands that its %w verbs wrap, as fmt's
// doPrintf collects them (p.wrappedErrs, in the order of the verbs, with
// repetitions), and whether the format has explicit argument indexes
// (p.reordered). Only the operand numbering of doPrintf is followed: the
// flags, widths, precisions and verbs, not the formatting.
func errorfWraps(format string, nargs int) (wrapped []int, reordered bool) {
	end := len(format)
	argNum := 0
	afterIndex := false
	goodArgNum := true
	// argNumber is pp.argNumber.
	argNumber := func(i int) (int, int, bool) {
		if len(format) <= i || format[i] != '[' {
			return argNum, i, false
		}
		reordered = true
		index, wid, ok := parseArgNumber(format[i:])
		if ok && 0 <= index && index < nargs {
			return index, i + wid, true
		}
		goodArgNum = false
		return argNum, i + wid, ok
	}
	// intFromArg advances over a '*' operand.
	intFromArg := func() {
		if argNum < nargs {
			argNum++
		}
	}
formatLoop:
	for i := 0; i < end; {
		goodArgNum = true
		for i < end && format[i] != '%' {
			i++
		}
		if i >= end {
			break
		}
		i++
	simpleFormat:
		for ; i < end; i++ {
			c := format[i]
			switch c {
			case '#', '0', '+', '-', ' ':
			default:
				if 'a' <= c && c <= 'z' && argNum < nargs {
					if c == 'w' {
						wrapped = append(wrapped, argNum)
					}
					argNum++
					i++
					continue formatLoop
				}
				break simpleFormat
			}
		}
		argNum, i, afterIndex = argNumber(i)
		if i < end && format[i] == '*' {
			i++
			intFromArg()
			afterIndex = false
		} else {
			var present bool
			_, present, i = parsenum(format, i, end)
			if afterIndex && present {
				goodArgNum = false
			}
		}
		if i+1 < end && format[i] == '.' {
			i++
			if afterIndex {
				goodArgNum = false
			}
			argNum, i, afterIndex = argNumber(i)
			if i < end && format[i] == '*' {
				i++
				intFromArg()
				afterIndex = false
			} else {
				_, _, i = parsenum(format, i, end)
			}
		}
		if !afterIndex {
			argNum, i, afterIndex = argNumber(i)
		}
		if i >= end {
			break
		}
		verb, size := utf8.DecodeRuneInString(format[i:])
		i += size
		switch {
		case verb == '%', !goodArgNum, argNum >= nargs:
		default:
			if verb == 'w' {
				wrapped = append(wrapped, argNum)
			}
			argNum++
		}
	}
	return wrapped, reordered
}

// parseArgNumber is fmt's: the index of "[n]" at the start of format.
func parseArgNumber(format string) (index int, wid int, ok bool) {
	if len(format) < 3 {
		return 0, 1, false
	}
	for i := 1; i < len(format); i++ {
		if format[i] == ']' {
			width, ok, newi := parsenum(format, 1, i)
			if !ok || newi != i {
				return 0, i + 1, false
			}
			return width - 1, i + 1, true
		}
	}
	return 0, 1, false
}

// parsenum is fmt's: the number at format[start:end].
func parsenum(s string, start, end int) (num int, isnum bool, newi int) {
	if start >= end {
		return 0, false, end
	}
	for newi = start; newi < end && '0' <= s[newi] && s[newi] <= '9'; newi++ {
		if num > 1e6 || num < -1e6 {
			return 0, false, end
		}
		num = num*10 + int(s[newi]-'0')
		isnum = true
	}
	return
}

// errorfCall rewrites a call of fmt.Errorf with a constant format, whose
// operands are not a spread slice, into a call of the model's errorfKnown
// with what the format determines (see errorfWraps): the number of %w
// verbs (as fmt counts them), the operands the error keeps (a bit mask, in
// the operands' order, which is fmt's), and whether no formatting is
// needed (then the text is the format). The model alone cannot read the
// format without a loop over its bytes.
func (e *encoder) errorfCall(c *ast.CallExpr, out map[string]any) {
	var id *ast.Ident
	switch f := ast.Unparen(c.Fun).(type) {
	case *ast.Ident:
		id = f
	case *ast.SelectorExpr:
		id = f.Sel
	}
	if id == nil || c.Ellipsis.IsValid() || len(c.Args) == 0 {
		return
	}
	f, ok := e.info.Uses[id].(*types.Func)
	if !ok || f.Pkg() == nil || f.Pkg().Path() != "fmt" || f.Name() != "Errorf" {
		return
	}
	m := e.loader.models["fmt"]
	if m == nil {
		return
	}
	known, _ := m.Scope().Lookup("errorfKnown").(*types.Func)
	tv, ok := e.info.Types[c.Args[0]]
	if known == nil || !ok || tv.Value == nil ||
		tv.Value.Kind() != constant.String {
		return
	}
	format := constant.StringVal(tv.Value)
	nargs := len(c.Args) - 1
	if nargs >= 64 {
		return
	}
	wrapped, reordered := errorfWraps(format, nargs)
	if reordered {
		slices.Sort(wrapped)
	}
	var mask uint64
	for _, argNum := range wrapped {
		mask |= 1 << argNum // repetitions are dropped, as fmt does
	}
	plain := nargs == 0 && !strings.Contains(format, "%")
	pos := out["_p"]
	constArg := func(t types.Type, value, kind string) map[string]any {
		return map[string]any{"_k": "BasicLit", "_p": pos, "_t": e.typ(t),
			"_cv": value, "_cvk": kind}
	}
	out["Fun"] = map[string]any{"_k": "Ident", "_p": pos, "Name": known.Name(),
		"_obj": e.objID(known), "_t": e.typ(known.Type())}
	args := []any{
		constArg(types.Typ[types.Int], strconv.Itoa(len(wrapped)), "int"),
		constArg(types.Typ[types.Uint64], strconv.FormatUint(mask, 10), "int"),
		constArg(types.Typ[types.Bool], strconv.FormatBool(plain), "bool"),
	}
	out["Args"] = append(args, out["Args"].([]any)...)
}
