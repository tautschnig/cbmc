# Multi-File TypeScript Support — Design Plan

## Overview

Currently the TypeScript frontend verifies single `.ts` files. This document
describes the plan for supporting multi-file TypeScript programs with
`import`/`export` statements.

## Architecture

### Current Single-File Flow

```
main.ts → Node.js parser → JSON AST → typescript_convertert → GOTO symbols
```

### Proposed Multi-File Flow

```
main.ts → Node.js parser (with imports) → JSON AST (all files) → converter → GOTO symbols
```

The key insight: the TypeScript Compiler API already resolves imports and
provides type information across files. We leverage this by having the
inline Node.js parser follow imports and emit a combined AST.

## Implementation Plan

### Phase 1: Parser Changes (inline Node.js script)

The inline parser script needs to:

1. **Resolve imports:** When encountering `import { foo } from './bar'`,
   use the TypeScript compiler's module resolution to find `bar.ts`.

2. **Emit combined AST:** Instead of emitting just the entry file's AST,
   emit all referenced files' ASTs in dependency order.

3. **Track file boundaries:** Each statement in the combined AST includes
   its source file path for source locations.

**Implementation:**
```javascript
// In the inline parser script:
const program = ts.createProgram([entryFile], compilerOptions);
const checker = program.getTypeChecker();

// Get all source files (excluding node_modules)
const sourceFiles = program.getSourceFiles()
  .filter(sf => !sf.isDeclarationFile && !sf.fileName.includes('node_modules'));

// Emit combined AST
const combined = { files: [] };
for (const sf of sourceFiles) {
  combined.files.push({
    fileName: sf.fileName,
    statements: sf.statements.map(n2j)
  });
}
```

### Phase 2: Converter Changes

1. **Process files in order:** Convert each file's statements sequentially.
   Imported symbols are available because files are ordered by dependency.

2. **Symbol namespacing:** Prefix symbols with the file path to avoid
   collisions: `typescript::./bar::foo` instead of `typescript::foo`.

3. **Export tracking:** Track which symbols are exported from each file.
   Only exported symbols are visible to importers.

4. **Import resolution:** When encountering `import { foo } from './bar'`,
   create an alias: `typescript::main::foo` → `typescript::./bar::foo`.

### Phase 3: Import Statement Handling

```typescript
// Supported import forms:
import { foo, bar } from './module';      // Named imports
import { foo as f } from './module';      // Renamed imports
import * as mod from './module';          // Namespace import
import defaultExport from './module';     // Default import
export { foo } from './module';           // Re-exports
```

**Converter implementation:**
```cpp
if(kind == "ImportDeclaration")
{
  // Get module specifier
  std::string module_path = json_string(json_member(node, "moduleSpecifier"));
  // Get imported names
  const jsont &imports = json_member(node, "importClause");
  // Create symbol aliases
  for(const auto &imp : named_imports)
  {
    irep_idt source_id{"typescript::" + module_path + "::" + imp.name};
    irep_idt local_id{"typescript::" + current_file + "::" + imp.local_name};
    // Create alias symbol
    symbol_table.get_writeable_ref(local_id) = symbol_table.lookup_ref(source_id);
  }
}
```

### Phase 4: Module Resolution

TypeScript has complex module resolution (node_modules, paths, baseUrl).
We support a subset:

1. **Relative imports:** `./foo`, `../bar` — resolve relative to current file.
2. **tsconfig.json paths:** Read `compilerOptions.paths` for path aliases.
3. **Index files:** `./dir` resolves to `./dir/index.ts`.

**Not supported initially:**
- `node_modules` resolution (third-party packages)
- Dynamic imports (`import()`)
- CommonJS (`require()`)

### Phase 5: Declaration Files (.d.ts)

For external libraries, support `.d.ts` declaration files:
- Parse type declarations to get function signatures
- Model declared functions as nondet (no body available)
- Support `@types/*` packages for type information

## Command-Line Interface

```bash
# Single file (current behavior)
cbmc main.ts

# Multi-file with explicit entry point
cbmc --entry main.ts src/foo.ts src/bar.ts

# Project mode (reads tsconfig.json)
cbmc --ts-project tsconfig.json

# With specific entry function
cbmc --ts-project tsconfig.json --function main
```

## Testing Strategy

1. **Phase 1 tests:** Two-file programs with simple imports.
2. **Phase 2 tests:** Circular imports, re-exports, namespace imports.
3. **Phase 3 tests:** tsconfig.json with paths, baseUrl.
4. **Phase 4 tests:** Declaration files for external types.

## Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| Circular imports | Process in topological order; break cycles at type level |
| Large projects | Only parse reachable files from entry point |
| node_modules | Model as nondet; support .d.ts for types |
| Path resolution | Start with relative imports only |
| Performance | Lazy parsing — only parse files when imported |

## Timeline

- Phase 1 (parser): ~100 lines of JavaScript changes
- Phase 2 (converter): ~50 lines of C++ changes
- Phase 3 (imports): ~80 lines of C++ changes
- Phase 4 (resolution): ~40 lines
- Phase 5 (declarations): ~60 lines

Total: ~330 lines of new code across 2 files.

## Example

```typescript
// math.ts
export function add(a: number, b: number): number {
  return a + b;
}

// main.ts
import { add } from './math';
console.assert(add(2, 3) === 5);
```

```bash
cbmc main.ts  # automatically finds and parses math.ts
```
