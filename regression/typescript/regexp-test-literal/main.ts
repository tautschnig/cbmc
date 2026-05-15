// ES2024 §22.2.5.13: RegExp.prototype.test with literal patterns.
function main(): void {
    const s = "hello world";
    // Inline regex
    console.assert(/hello/.test(s));
    console.assert(/world/.test(s));
    console.assert(!/xyz/.test(s));
    // Via variable
    const re = /ello/;
    console.assert(re.test(s));
    console.assert(!re.test("goodbye"));
}
main();
