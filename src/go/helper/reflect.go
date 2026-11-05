// What the model of package reflect needs of the type table.

package main

import (
	"go/types"
	"slices"
	"strings"
)

// reflectUses returns the functions and methods of package reflect that
// code other than reflect's model uses ("SliceOf", "Value.Call", ...),
// sorted, so that the front-end generates what they need only then.
func (e *encoder) reflectUses() []string {
	if !e.reflects() {
		return nil
	}
	if e.reflectUsed != nil {
		return e.reflectUsed
	}
	seen := map[string]bool{}
	for id, obj := range e.info.Uses {
		f, ok := obj.(*types.Func)
		if !ok || f.Pkg() == nil || f.Pkg().Path() != "reflect" {
			continue
		}
		file := e.fset.Position(id.Pos()).Filename
		if strings.HasPrefix(file, modelPrefix+"reflect/") {
			continue
		}
		name := f.Name()
		if recv := f.Signature().Recv(); recv != nil {
			t := recv.Type()
			if p, ok := t.(*types.Pointer); ok {
				t = p.Elem()
			}
			if n, ok := t.(*types.Named); ok {
				name = n.Obj().Name() + "." + name
			}
		}
		seen[name] = true
	}
	e.reflectUsed = []string{}
	for name := range seen {
		e.reflectUsed = append(e.reflectUsed, name)
	}
	slices.Sort(e.reflectUsed)
	return e.reflectUsed
}

// usesReflect reports whether code other than reflect's model uses
// function or method name of package reflect (see reflectUses).
func (e *encoder) usesReflect(name string) bool {
	_, found := slices.BinarySearch(e.reflectUses(), name)
	return found
}

// reflects reports whether the program uses package reflect (whose model,
// models/reflect, replaces it): the type table then describes the types
// as reflect does (see reflectInfo).
func (e *encoder) reflects() bool {
	return e.loader != nil && e.loader.byPath["reflect"] != nil
}

// reflectInfo adds to the entry of t what reflect reports of it, as gc
// does on the target: "rstr" (Type.String, with package names), "rname"
// and "rpkg" (Type.Name and Type.PkgPath of named and predeclared types),
// "size", "align" and "falign" (Type.Size, Align and FieldAlign), the
// fields' "offsets" of structs (StructField.Offset), "ptr", the entry of
// the pointer type to t (for reflect.New, PointerTo and Value.Addr, which
// need it even if the program does not mention it), and "slice", the
// entry of the slice type of t if the program uses reflect.SliceOf.
func (e *encoder) reflectInfo(t types.Type, entry map[string]any) {
	index := e.typeIdx[t]
	method := false
	switch x := t.(type) {
	case *types.Tuple, *types.TypeParam:
		return
	case *types.Basic:
		if x.Info()&types.IsUntyped != 0 {
			return
		}
	case *types.Signature:
		// Method values (types.Selection.Type) keep their receiver, which
		// is not part of the type (Go1.27 §Type_identity).
		if x.TypeParams().Len() > 0 {
			return
		}
		if x.Recv() != nil {
			t = types.NewSignatureType(nil, nil, nil, x.Params(),
				x.Results(), x.Variadic())
			method = true
		}
	case *types.Named:
		if x.TypeParams().Len() > 0 && x.TypeArgs().Len() == 0 {
			return
		}
	}
	if k, _ := entry["k"].(string); k == "unknown" {
		return
	}
	qualifier := func(p *types.Package) string {
		return strings.TrimPrefix(p.Name(), modelPrefix)
	}
	entry["rstr"] = types.TypeString(t, qualifier)
	switch x := t.(type) {
	case *types.Named:
		name := x.Obj().Name()
		args := typeList(x.TypeArgs())
		local, isLocal := e.localArgs[x]
		if isLocal {
			// gc names a type declared in an instance of a generic
			// function or method after that instance's type arguments.
			args = local
		}
		if len(args) > 0 {
			var list []string
			for _, a := range args {
				list = append(list, types.TypeString(a, (*types.Package).Path))
			}
			name += "[" + strings.Join(list, ",") + "]"
		}
		if isLocal {
			entry["rstr"] = qualifier(x.Obj().Pkg()) + "." + name
		}
		entry["rname"] = name
		if p := x.Obj().Pkg(); p != nil {
			entry["rpkg"] = strings.TrimPrefix(p.Path(), modelPrefix)
		}
	case *types.Basic:
		if x.Kind() != types.UnsafePointer {
			entry["rname"] = x.Name()
		} else {
			entry["rname"] = "Pointer"
			entry["rpkg"] = "unsafe"
		}
	}
	entry["size"] = e.sizes.Sizeof(t)
	entry["align"] = e.sizes.Alignof(t)
	entry["falign"] = e.sizes.Alignof(t)
	if s, ok := t.Underlying().(*types.Struct); ok {
		var fields []*types.Var
		for j := 0; j < s.NumFields(); j++ {
			fields = append(fields, s.Field(j))
		}
		entry["offsets"] = e.sizes.Offsetsof(fields)
	}
	// The types added for these links (derived types) get no links of
	// their own, except the pointer type to an added slice type (for
	// reflect.New(reflect.SliceOf(t))), so that this ends.
	derived := e.reflectDerived[index]
	if derived > 1 || method {
		return
	}
	if derived == 0 && (e.usesReflect("SliceOf") ||
		e.usesReflect("Value.Slice") || e.usesReflect("Value.Slice3")) {
		e.derivedType(types.NewSlice(t), 1)
		entry["slice"] = e.typ(types.NewSlice(t))
	}
	if !e.usesPointerTypes() {
		return
	}
	e.derivedType(types.NewPointer(t), derived+1)
	entry["ptr"] = e.typ(types.NewPointer(t))
}

// usesPointerTypes reports whether the program uses the members of reflect
// that make pointer types (see the front-end's finish_reflect).
func (e *encoder) usesPointerTypes() bool {
	for _, name := range []string{"New", "NewAt", "Value.Addr", "PtrTo",
		"PointerTo"} {
		if e.usesReflect(name) {
			return true
		}
	}
	return false
}

// derivedType records that t is added to the type table for a link of
// reflectInfo, at depth depth, unless it is there already.
func (e *encoder) derivedType(t types.Type, depth int) {
	n := len(e.types)
	if i := e.typ(t); i >= n {
		e.reflectDerived[i] = depth
	}
}

// identical is types.Identical, for which the types of a package in
// checkedAgainstReal are those of its model (see replacementType), and
// those of a source model those of the real package (see realType): the
// program's types mention the real ones, the model's own code its. (For
// example, the signature of a method of an interface type that a source
// model declares mentions the model's types, while the method sets of the
// real types that implement it, from export data, mention the real ones.)
func (e *encoder) identical(x, y types.Type) bool {
	if types.Identical(x, y) {
		return true
	}
	if e.loader == nil {
		return false
	}
	return types.Identical(e.replaced(x), e.replaced(y))
}

// replaced returns t with the named types of the packages in
// checkedAgainstReal replaced by their models' (see replacementType), and
// those of source models by the real packages' (see realType).
func (e *encoder) replaced(t types.Type) types.Type {
	if r, ok := e.replacedCache[t]; ok {
		return r
	}
	e.replacedCache[t] = t // recursive types end here
	r := e.replace(t)
	e.replacedCache[t] = r
	return r
}

func (e *encoder) replace(t types.Type) types.Type {
	tuple := func(tup *types.Tuple) (*types.Tuple, bool) {
		if tup == nil {
			return nil, false
		}
		changed := false
		var vars []*types.Var
		for i := 0; i < tup.Len(); i++ {
			v := tup.At(i)
			vt := e.replaced(v.Type())
			if vt != v.Type() {
				changed = true
				v = types.NewParam(v.Pos(), v.Pkg(), v.Name(), vt)
			}
			vars = append(vars, v)
		}
		return types.NewTuple(vars...), changed
	}
	switch x := types.Unalias(t).(type) {
	case *types.Named:
		if n := x.Obj().Pkg(); n != nil && checkedAgainstReal[n.Path()] {
			if r := e.replacementType(x); r != nil {
				return r
			}
		}
		return e.realType(x)
	case *types.Pointer:
		if el := e.replaced(x.Elem()); el != x.Elem() {
			return types.NewPointer(el)
		}
	case *types.Slice:
		if el := e.replaced(x.Elem()); el != x.Elem() {
			return types.NewSlice(el)
		}
	case *types.Array:
		if el := e.replaced(x.Elem()); el != x.Elem() {
			return types.NewArray(el, x.Len())
		}
	case *types.Map:
		k, v := e.replaced(x.Key()), e.replaced(x.Elem())
		if k != x.Key() || v != x.Elem() {
			return types.NewMap(k, v)
		}
	case *types.Chan:
		if el := e.replaced(x.Elem()); el != x.Elem() {
			return types.NewChan(x.Dir(), el)
		}
	case *types.Signature:
		if x.TypeParams().Len() > 0 {
			return t
		}
		params, pc := tuple(x.Params())
		results, rc := tuple(x.Results())
		if pc || rc {
			return types.NewSignatureType(nil, nil, nil, params, results,
				x.Variadic())
		}
	case *types.Tuple:
		if r, changed := tuple(x); changed {
			return r
		}
	case *types.Struct:
		changed := false
		var fields []*types.Var
		var tags []string
		for i := 0; i < x.NumFields(); i++ {
			f := x.Field(i)
			ft := e.replaced(f.Type())
			if ft != f.Type() {
				changed = true
				f = types.NewField(f.Pos(), f.Pkg(), f.Name(), ft, f.Embedded())
			}
			fields = append(fields, f)
			tags = append(tags, x.Tag(i))
		}
		if changed {
			return types.NewStruct(fields, tags)
		}
	}
	return t
}
