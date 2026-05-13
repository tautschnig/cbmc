// Regression for: ES2024 §7.1.2 ToBoolean coercion of strings.
// "" → false, any non-empty string → true. Prior implementation
// typecast the string struct directly to bool (producing nondet);
// now routes through the ts_to_boolean helper which emits
// `length != 0`.
function main(): void {
    // Empty string is falsy
    if ("") { console.assert(false); }
    console.assert(!"" === true);

    // Non-empty string is truthy
    const s = "hello";
    if (s) { console.assert(true); } else { console.assert(false); }
    console.assert(!"x" === false);

    // In conditional context (annotate `: number` to avoid
    // TS's literal-union inference 1|2, which our tagged-union
    // representation handles imprecisely — see
    // doc/typescript-verification-guide.md edge cases).
    const b1: number = "" ? 1 : 2;
    console.assert(b1 === 2);
    const b2: number = "x" ? 1 : 2;
    console.assert(b2 === 1);

    // In || operator
    const fallback = "" || "backup";
    console.assert(fallback === "backup");
}
main();
