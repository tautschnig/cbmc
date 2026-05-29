// Phase 2 RegExp: character classes [abc], [^abc], [a-z]
function main(): void {
    // Positive class
    console.assert(/[abc]/.test("a"));
    console.assert(/[abc]/.test("b"));
    console.assert(/[abc]/.test("c"));
    console.assert(!/[abc]/.test("d"));

    // Negated class
    console.assert(!/[^abc]/.test("a"));
    console.assert(/[^abc]/.test("d"));

    // Range
    console.assert(/[a-z]/.test("m"));
    console.assert(!/[a-z]/.test("M"));
    console.assert(/[A-Z]/.test("M"));
    console.assert(/[0-9]/.test("5"));
    console.assert(!/[0-9]/.test("a"));

    // Multiple ranges in one class
    console.assert(/[a-zA-Z0-9_]/.test("z"));
    console.assert(/[a-zA-Z0-9_]/.test("Z"));
    console.assert(/[a-zA-Z0-9_]/.test("5"));
    console.assert(/[a-zA-Z0-9_]/.test("_"));
    console.assert(!/[a-zA-Z0-9_]/.test("-"));

    // Class plus quantifier
    console.assert(/[abc]+/.test("aabbcc"));
    console.assert(!/[abc]+/.test("xyz"));
}
main();
