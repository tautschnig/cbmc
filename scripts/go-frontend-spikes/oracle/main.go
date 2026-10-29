// Oracle for spike B: per-file counts of AST node kinds produced by
// go/parser, for structural comparison with the native C++ parser.
// Reads file paths on stdin; prints "path kind=count ..." per file.
package main

import (
	"bufio"
	"fmt"
	"go/ast"
	"go/parser"
	"go/token"
	"os"
	"sort"
)

// Mapping from go/ast node types to the native parser's kind names.
var kinds = map[string]string{
	"*ast.FuncDecl": "FuncDecl", "*ast.FuncLit": "FuncLit",
	"*ast.CompositeLit": "CompositeLit", "*ast.CallExpr": "Call",
	"*ast.TypeSpec": "TypeSpec", "*ast.IfStmt": "If",
	"*ast.SwitchStmt": "Switch", "*ast.TypeSwitchStmt": "Switch",
	"*ast.ForStmt": "For", "*ast.RangeStmt": "For",
	"*ast.KeyValueExpr": "KeyValue", "*ast.ReturnStmt": "Return",
	"*ast.LabeledStmt": "Labeled", "*ast.SendStmt": "Send",
	"*ast.IncDecStmt": "IncDec", "*ast.SliceExpr": "Slice",
	"*ast.TypeAssertExpr": "TypeAssert", "*ast.SelectStmt": "Select",
}

func main() {
	sc := bufio.NewScanner(os.Stdin)
	for sc.Scan() {
		path := sc.Text()
		f, err := parser.ParseFile(token.NewFileSet(), path, nil,
			parser.SkipObjectResolution)
		if err != nil {
			fmt.Println(path, "ERROR")
			continue
		}
		counts := map[string]int{}
		ast.Inspect(f, func(n ast.Node) bool {
			if k, ok := kinds[fmt.Sprintf("%T", n)]; ok {
				counts[k]++
			}
			return true
		})
		var keys []string
		for k := range counts {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		fmt.Print(path)
		for _, k := range keys {
			fmt.Printf(" %s=%d", k, counts[k])
		}
		fmt.Println()
	}
}
