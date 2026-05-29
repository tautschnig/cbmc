// =====================================================================
// Harness Template: Path Traversal (DEFENSIVE form)
// =====================================================================
//
// **When to use**:
//   The function under audit takes a file path argument and
//   performs a filesystem operation on it. Without a guard, attacker
//   inputs containing `..` segments (or absolute paths in
//   sandbox-only contexts) escape the intended directory.
//
//   Threat model: the application caller has already validated
//   the path. We're verifying the protected sink is reachable only
//   with safe paths.
//
// **What this verifies**:
//   __CPROVER_assert_no_path_traversal holds for the constrained
//   nondet input.
//
// **Customise**:
//   - Replace `read_file` with the real filesystem call.
//   - The harness's nondet source should match the actual caller's
//     input shape (URL component, JSON config, env var, etc.).

function harness(): void {
    const path = nondet_string();
    // Application contract: caller has constrained the path to a
    // known-safe value. Single-value assume — for multi-value
    // contracts, repeat the harness.
    __CPROVER_assume(path === "data/config.json");
    __CPROVER_assert_no_path_traversal(path);
}
harness();
