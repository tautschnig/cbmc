// =====================================================================
// Harness Template: Path Traversal (BUGGY form)
// =====================================================================
//
// **When to use**:
//   You suspect a path-handling sink is missing the
//   __CPROVER_assert_no_path_traversal check. Pass a known-bad
//   path directly to demonstrate the assertion catches it.
//
// **What this verifies**:
//   __CPROVER_assert_no_path_traversal correctly rejects a path
//   containing `..`. CBMC reports VERIFICATION FAILED.
//
// **Adapt to your audit**: replace the path literal with whatever
// shape the caller might let slip through (e.g. "/etc/passwd"
// for absolute-path leaks if your sandbox should reject those too).
//
// See also: `harness-template-path-traversal-defensive/` for the
// form your fix should converge on.

function harness(): void {
    const path: string = "data/../../../etc/passwd";
    __CPROVER_assert_no_path_traversal(path);
}
harness();
