// Integration test: defensive picomatch with allowlist check.
//
// Mirrors the picomatch@2.3.2 fix: POSIX class names are validated
// against an allowlist before lookup.

function harness_safe(): void {
    const POSIX_CLASSES: string[] = [
        "alnum", "alpha", "ascii", "blank", "cntrl",
        "digit", "graph", "lower", "print", "punct",
        "space", "upper", "word", "xdigit"
    ];
    // Legitimate class name from a normal glob pattern.
    const className: string = "alpha";
    __CPROVER_assert_in_allowlist(className, POSIX_CLASSES);
}
harness_safe();
