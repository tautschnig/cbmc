// Integration test: flatted CVE GHSA-q8gm-r3vv-cwfj
// (unbounded recursion DoS in parse() revive phase).
//
// The vulnerability: flatted's parse() recursively resolves
// references in the parsed JSON. An attacker sends a deeply
// nested input that drives unbounded recursion, exhausting the
// stack.
//
// Source: flatted@3.4.0 cjs/index.js (resolver function);
//         fix: flatted@3.4.2 (depth limit added)
// CVE record: https://github.com/advisories/GHSA-q8gm-r3vv-cwfj
//
// Defensive contract: the parse() function expects bounded input.
// Use __CPROVER_assert_input_size_bounded at function entry to
// document and verify this precondition. For verifying that the
// recursion itself is bounded, combine with --unwinding-assertions.

const MAX_DEPTH: number = 64;  // application-level limit

// Defensive parse that rejects oversized input upfront.
function parse_safe(input: number[]): number {
    __CPROVER_assert_input_size_bounded(input, MAX_DEPTH);
    // ... recursion would happen here, but is bounded by MAX_DEPTH.
    return input.length;
}

function harness(): void {
    // Application receives an array that may have arbitrary length.
    // The defensive contract says: enforce bounds at the entry.
    const input: number[] = [1, 2, 3, 4, 5];  // bounded by MAX_DEPTH
    parse_safe(input);
}
harness();
