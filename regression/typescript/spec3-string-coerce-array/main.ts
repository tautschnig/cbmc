// Regression for: ES2024 §13.15.3 / §23.1.3.32. When concatenating
// a string with an array, the array is coerced via toString() which
// is equivalent to .join(","). Prior implementation didn't handle
// the array case in the coerce_to_string helper, leaving the result
// underspecified.
function main(): void {
    const arr = [1, 2, 3];
    const s = "" + arr;
    console.assert(s === "1,2,3");

    // With prefix
    const labelled = "items: " + [10, 20];
    console.assert(labelled === "items: 10,20");

    // Empty array → empty join → empty string
    const empty: number[] = [];
    const se = "x:" + empty;
    console.assert(se === "x:");
}
main();
