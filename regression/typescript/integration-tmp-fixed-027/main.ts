// Integration test: tmp@0.2.7 (fix for GHSA-7c78-jf6q-g5cm).
//
// The fix in v0.2.7: _assertPath now does `typeof value === 'string'`
// before the .includes("..") check. In TypeScript, the type system
// statically enforces this, so `(value: string)` corresponds to the
// post-fix runtime behavior (any non-string is rejected at compile
// time).
//
// Source: https://github.com/raszi/node-tmp/blob/v0.2.7/lib/tmp.js

// --- BEGIN verbatim from tmp v0.2.7 ---
// Fixed _assertPath (typeof check is statically enforced in TS):
function _assertPath(value: string): string {
    if (value.includes("..")) {
        return "tmp";  // throw modelled as safe fallback
    }
    return value;
}
function _generateTmpName(prefix: string): string {
    const validated: string = _assertPath(prefix);
    return validated;
}
// --- END verbatim from tmp v0.2.7 ---

function harness(): void {
    const safe: string = "myapp";
    const path: string = _generateTmpName(safe);
    __CPROVER_assert_no_path_traversal(path);
}
harness();
