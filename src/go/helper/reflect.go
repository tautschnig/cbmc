// What the model of package reflect needs of the type table.

package main

import (
	"go/types"
	"strings"
)

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
// fields' "offsets" of structs (StructField.Offset), and "ptr", the entry
// of the pointer type to t (for reflect.New, PointerTo and Value.Addr,
// which need it even if the program does not mention it).
func (e *encoder) reflectInfo(t types.Type, entry map[string]any) {
	switch x := t.(type) {
	case *types.Tuple, *types.TypeParam:
		return
	case *types.Basic:
		if x.Info()&types.IsUntyped != 0 {
			return
		}
	case *types.Signature:
		if x.Recv() != nil || x.TypeParams().Len() > 0 {
			return
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
	// Pointers to pointers are as far as it goes (reflect.New of a
	// pointer type).
	if p, ok := t.(*types.Pointer); ok {
		if _, ok := p.Elem().(*types.Pointer); ok {
			return
		}
	}
	entry["ptr"] = e.typ(types.NewPointer(t))
}
