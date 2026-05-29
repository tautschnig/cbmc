// Phase 2 RegExp: `.` (any character except newline)
function main(): void {
    // /a.b/ matches "axb", "a1b", "a-b" etc.
    console.assert(/a.b/.test("axb"));
    console.assert(/a.b/.test("a1b"));
    console.assert(/a.b/.test("a-b"));

    // But not "ab" (need exactly one char in between).
    console.assert(!/a.b/.test("ab"));

    // Or strings that don't have an a followed by anything followed by b.
    console.assert(!/a.b/.test("xy"));

    // Three-dot pattern matches any 3-char window.
    console.assert(/.../.test("xyz"));
    console.assert(!/.../.test("xy"));
}
main();
