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
		if args := x.TypeArgs(); args.Len() > 0 {
			var list []string
			for j := 0; j < args.Len(); j++ {
				list = append(list, types.TypeString(args.At(j),
					(*types.Package).Path))
			}
			name += "[" + strings.Join(list, ",") + "]"
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
	e.derivedType(types.NewPointer(t), derived+1)
	entry["ptr"] = e.typ(types.NewPointer(t))
}

// derivedType records that t is added to the type table for a link of
// reflectInfo, at depth depth, unless it is there already.
func (e *encoder) derivedType(t types.Type, depth int) {
	n := len(e.types)
	if i := e.typ(t); i >= n {
		e.reflectDerived[i] = depth
	}
}
