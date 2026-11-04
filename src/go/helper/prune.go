// Pruning of the functions that the program cannot reach.

package main

import "strings"

// prune removes from the encoded files asts the declarations of functions
// (not methods) that the program cannot reach, so that the front-end does
// not load and convert them: most of the source models are functions that
// a program does not call (math's for a program that uses strconv).
//
// The roots are the functions of the program's own package (main, and the
// entry function of --function; unsupported constructs in them are
// reported even where unreachable), the init functions, the initialisers of package
// variables (packages), every method (interface dispatch, method sets and
// reflection reach methods without naming them), and the runtime package
// cprover/rt (whose functions the front-end calls by name). A function is
// reachable if a reachable declaration or initialiser refers to its object
// id (in "_obj", "_impl" or a selection's "obj"), which covers calls,
// function values, and the helper's rewrites (instances, models, Errorf).
func prune(asts []any, packages []any, types []any, mainPkg string) {
	funcs := map[string]map[string]any{}
	var roots []string
	for _, a := range asts {
		file, _ := a.(map[string]any)
		decls, _ := file["Decls"].([]any)
		for _, d := range decls {
			decl, _ := d.(map[string]any)
			if decl["_k"] != "FuncDecl" {
				continue
			}
			name, _ := decl["Name"].(map[string]any)
			id, _ := name["_obj"].(string)
			if id == "" {
				continue
			}
			funcs[id] = decl
			if decl["Recv"] != nil || strings.HasPrefix(id, mainPkg+".") ||
				strings.Contains(id, ".init#") ||
				strings.HasPrefix(id, runtimePath+".") {
				roots = append(roots, id)
			}
		}
	}
	reached := map[string]bool{}
	var work []string
	refer := func(n any) {
		var walk func(n any)
		walk = func(n any) {
			switch x := n.(type) {
			case map[string]any:
				for _, k := range []string{"_obj", "_impl"} {
					if id, ok := x[k].(string); ok && !reached[id] {
						work = append(work, id)
					}
				}
				if sel, ok := x["_sel"].(map[string]any); ok {
					if id, ok := sel["obj"].(string); ok && !reached[id] {
						work = append(work, id)
					}
				}
				for _, v := range x {
					walk(v)
				}
			case []any:
				for _, v := range x {
					walk(v)
				}
			}
		}
		walk(n)
	}
	work = append(work, roots...)
	for _, p := range packages {
		pkg, _ := p.(map[string]any)
		inits, _ := pkg["init_order"].([]any)
		for _, i := range inits {
			refer(i)
		}
	}
	// Methods in method sets (of types of the type table) as well.
	for _, t := range types {
		entry, _ := t.(map[string]any)
		mset, _ := entry["mset"].([]any)
		for _, m := range mset {
			if id, ok := m.(map[string]any)["obj"].(string); ok {
				work = append(work, id)
			}
		}
	}
	for len(work) > 0 {
		id := work[len(work)-1]
		work = work[:len(work)-1]
		if reached[id] {
			continue
		}
		reached[id] = true
		if decl, ok := funcs[id]; ok {
			refer(decl)
		}
	}
	for _, a := range asts {
		file, _ := a.(map[string]any)
		decls, ok := file["Decls"].([]any)
		if !ok {
			continue
		}
		kept := decls[:0]
		for _, d := range decls {
			decl, _ := d.(map[string]any)
			if decl["_k"] == "FuncDecl" {
				name, _ := decl["Name"].(map[string]any)
				if id, _ := name["_obj"].(string); id != "" && !reached[id] {
					continue
				}
			}
			kept = append(kept, d)
		}
		file["Decls"] = kept
	}
}
