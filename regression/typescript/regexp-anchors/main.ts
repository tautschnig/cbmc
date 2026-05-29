// Phase 2 RegExp: anchors `^` (begin) and `$` (end)
function main(): void {
    // ^ alone — pin to start
    console.assert(/^foo/.test("foobar"));
    console.assert(!/^foo/.test("xfoobar"));

    // $ alone — pin to end
    console.assert(/bar$/.test("foobar"));
    console.assert(!/bar$/.test("foobarx"));

    // Both — exact match
    console.assert(/^foo$/.test("foo"));
    console.assert(!/^foo$/.test("foox"));
    console.assert(!/^foo$/.test("xfoo"));

    // Anchor with quantifiers and char classes
    console.assert(/^[a-z]+$/.test("hello"));
    console.assert(!/^[a-z]+$/.test("Hello"));
    console.assert(!/^[a-z]+$/.test("hello!"));

    // Realistic: bearer-token style header
    console.assert(/^Bearer /.test("Bearer abc123"));
    console.assert(!/^Bearer /.test("X-Bearer abc123"));

    // Realistic: trailing extension
    console.assert(/\.test\.ts$/.test("foo.test.ts"));
    console.assert(!/\.test\.ts$/.test("foo.test.tsx"));
}
main();
