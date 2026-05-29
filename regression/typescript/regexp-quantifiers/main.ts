// Phase 2 RegExp: quantifiers `*`, `+`, `?`
function main(): void {
    // * — zero or more
    console.assert(/a*/.test(""));        // zero a's matches
    console.assert(/a*/.test("aaa"));
    console.assert(/ab*c/.test("ac"));    // zero b's
    console.assert(/ab*c/.test("abc"));   // one b
    console.assert(/ab*c/.test("abbbc")); // many b's
    console.assert(!/ab*c/.test("ad"));   // no c

    // + — one or more
    console.assert(!/a+/.test(""));
    console.assert(/a+/.test("a"));
    console.assert(/a+/.test("aaa"));
    console.assert(/ab+c/.test("abc"));
    console.assert(/ab+c/.test("abbc"));
    console.assert(!/ab+c/.test("ac"));   // requires at least one b

    // ? — optional (zero or one)
    console.assert(/colou?r/.test("color"));
    console.assert(/colou?r/.test("colour"));
    console.assert(!/colou?r/.test("colouur"));   // not two u's
}
main();
