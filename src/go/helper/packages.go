// Packages from source: the program's package and the user packages it
// imports.

package main

import (
	"bufio"
	"fmt"
	"go/ast"
	"go/build"
	"go/parser"
	"go/token"
	"go/types"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// A package converted from source.
type sourcePkg struct {
	path      string
	files     []*ast.File
	pkg       *types.Package
	initOrder []*types.Initializer
}

// The packages loaded from source, and how import paths are resolved.
type loader struct {
	fset    *token.FileSet
	conf    *types.Config
	info    *types.Info
	enc     *encoder
	names   *[]string    // absolute file names, indexed as in positions
	files   *[]*ast.File // the files, in the same order
	modRoot string       // directory of the main module's go.mod, or ""
	modPath string       // the main module's path
	srcDir  string       // directory of the program's files
	byPath  map[string]*sourcePkg
	loading map[string]bool
	order   []*sourcePkg // in the order they were type-checked
	models  map[string]*types.Package
	real    map[string]*types.Package // standard-library packages, by path
	failed  *bool
}

// findModule finds the go.mod enclosing dir and reads its module path.
func findModule(dir string) (root, modPath string) {
	for d := dir; ; d = filepath.Dir(d) {
		f, err := os.Open(filepath.Join(d, "go.mod"))
		if err == nil {
			defer f.Close()
			s := bufio.NewScanner(f)
			for s.Scan() {
				fields := strings.Fields(s.Text())
				if len(fields) >= 2 && fields[0] == "module" {
					return d, strings.Trim(fields[1], `"`)
				}
			}
			return d, ""
		}
		if filepath.Dir(d) == d {
			return "", ""
		}
	}
}

// addFile registers a parsed file under name.
func (l *loader) addFile(name string, f *ast.File) {
	l.enc.fileIdx[name] = len(*l.names)
	*l.names = append(*l.names, name)
	*l.files = append(*l.files, f)
}

// check type-checks files as package path.
func (l *loader) check(path string, files []*ast.File) *sourcePkg {
	sp := &sourcePkg{path: path, files: files}
	pkg, _ := l.conf.Check(path, l.fset, files, l.info)
	sp.pkg = pkg
	l.byPath[path] = sp
	// InitOrder is reused by the next check (also of packages that this
	// one imports, which are checked before it finishes).
	sp.initOrder = append([]*types.Initializer(nil), l.info.InitOrder...)
	l.order = append(l.order, sp)
	return sp
}

// loadDir loads the package in directory dir (Go1.27 §Packages: "A
// package in turn is constructed from one or more source files"): its .go
// files for the target, excluding tests, as go build selects them.
func (l *loader) loadDir(path, dir string) (*types.Package, error) {
	bp, err := build.Default.ImportDir(dir, 0)
	if err != nil {
		return nil, err
	}
	if len(bp.CgoFiles) > 0 {
		return nil, fmt.Errorf("package %s uses cgo, which is not supported",
			path)
	}
	var files []*ast.File
	for _, name := range bp.GoFiles {
		abs := filepath.Join(bp.Dir, name)
		f, err := parser.ParseFile(l.fset, abs, nil, parser.SkipObjectResolution)
		if err != nil {
			return nil, err
		}
		l.addFile(abs, f)
		files = append(files, f)
	}
	return l.check(path, files).pkg, nil
}

// importSource loads import path p from source if it is a user package:
// one of the main module, or (with the go command) of another module.
func (l *loader) importSource(p string) (*types.Package, bool, error) {
	if l.loading[p] {
		// Go1.27 §Import_declarations: "It is illegal for a package to
		// import itself, directly or indirectly".
		return nil, true, fmt.Errorf("import cycle not allowed")
	}
	if sp, ok := l.byPath[p]; ok {
		return sp.pkg, true, nil
	}
	dir := ""
	switch {
	case l.modPath != "" && (p == l.modPath ||
		strings.HasPrefix(p, l.modPath+"/")):
		dir = filepath.Join(l.modRoot, filepath.FromSlash(
			strings.TrimPrefix(strings.TrimPrefix(p, l.modPath), "/")))
	case strings.Contains(strings.SplitN(p, "/", 2)[0], "."):
		// Other modules (their first path element is a domain name), found
		// by the go command.
		bp, err := build.Default.Import(p, l.srcDir, build.FindOnly)
		if err != nil {
			return nil, true, err
		}
		dir = bp.Dir
	default:
		return nil, false, nil // the standard library
	}
	l.loading[p] = true
	defer delete(l.loading, p)
	pkg, err := l.loadDir(p, dir)
	return pkg, true, err
}

// runtime loads the front-end's runtime package (runtime/rt.go).
func (l *loader) runtime() (*types.Package, error) {
	if sp, ok := l.byPath[runtimePath]; ok {
		return sp.pkg, nil
	}
	f, err := parser.ParseFile(l.fset, "cprover/rt/rt.go", runtimeSource,
		parser.SkipObjectResolution)
	if err != nil {
		return nil, err
	}
	l.addFile("cprover/rt/rt.go", f)
	return l.check(runtimePath, []*ast.File{f}).pkg, nil
}

// initOrder sorts the packages for initialisation (Go1.27
// §Package_initialization): "Given the list of all packages, sorted by
// import path, in each step the first uninitialized package in the list for
// which all imported packages (if any) are already initialized is
// initialized."
func initOrder(pkgs []*sourcePkg) []*sourcePkg {
	sorted := append([]*sourcePkg(nil), pkgs...)
	sort.Slice(sorted, func(i, j int) bool {
		return sorted[i].path < sorted[j].path
	})
	inList := map[string]bool{}
	for _, p := range pkgs {
		inList[p.path] = true
	}
	done := map[string]bool{}
	var out []*sourcePkg
	for len(out) < len(sorted) {
		for _, p := range sorted {
			if done[p.path] {
				continue
			}
			ready := true
			for _, imp := range p.pkg.Imports() {
				if inList[imp.Path()] && !done[imp.Path()] {
					ready = false
				}
			}
			if ready {
				done[p.path] = true
				out = append(out, p)
				break
			}
		}
	}
	return out
}
