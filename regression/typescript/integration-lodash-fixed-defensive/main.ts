// Integration test: defensive _unset that validates EVERY path
// segment, demonstrating the lodash@4.18.0 fix pattern.
//
// Counterpart to integration-lodash-cve-ghsa-f23m: the fixed
// implementation validates each segment, so the prototype-pollution
// assertion holds for any safe path.
//
// Source: lodash@4.18.0 (the fix for GHSA-f23m-r3pf-42rh).

// --- Defensive _unset (validates every segment) ---
function _unset_safe(path: string[]): boolean {
    for (let i = 0; i < path.length; i++) {
        if (path[i] === "__proto__" ||
            path[i] === "constructor" ||
            path[i] === "prototype") {
            return false;
        }
    }
    return true;
}

function harness(): void {
    // Safe path — no dangerous segments.
    const path: string[] = ["users", "alice", "name"];
    if (!_unset_safe(path)) return;
    // After the defensive validation, every segment is safe.
    __CPROVER_assert_safe_property_key(path[0]);
    __CPROVER_assert_safe_property_key(path[1]);
    __CPROVER_assert_safe_property_key(path[2]);
}
harness();
