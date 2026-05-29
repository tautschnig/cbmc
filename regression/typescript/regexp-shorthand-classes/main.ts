// Phase 2 RegExp: shorthand classes \d, \w, \s and negated forms
function main(): void {
    // \d — digit
    console.assert(/\d/.test("5"));
    console.assert(!/\d/.test("a"));
    console.assert(/\d+/.test("123"));

    // \D — non-digit
    console.assert(!/\D/.test("5"));
    console.assert(/\D/.test("a"));

    // \w — word char (letters, digits, underscore)
    console.assert(/\w/.test("a"));
    console.assert(/\w/.test("Z"));
    console.assert(/\w/.test("5"));
    console.assert(/\w/.test("_"));
    console.assert(!/\w/.test("-"));

    // \W — non-word
    console.assert(/\W/.test("-"));
    console.assert(/\W/.test(" "));
    console.assert(!/\W/.test("a"));

    // \s — whitespace
    console.assert(/\s/.test(" "));
    console.assert(/\s/.test("\t"));
    console.assert(/\s/.test("\n"));
    console.assert(!/\s/.test("a"));

    // \S — non-whitespace
    console.assert(/\S/.test("a"));
    console.assert(!/\S/.test(" "));

    // Combined
    console.assert(/\d\d\d/.test("123"));
    console.assert(/\w+/.test("foo_bar123"));
    console.assert(/\s+/.test("   "));
}
main();
