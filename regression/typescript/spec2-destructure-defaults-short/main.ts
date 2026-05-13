// Regression for: ES2024 §14.3.3 destructuring defaults when the
// source array is shorter than the pattern, OR when a renamed
// object property is missing from an empty source.
function main(): void {
    // Array destructure: source shorter than pattern, defaults fill
    // in the missing tail.
    const [a = 10, b = 20, c = 30] = [1, 2];
    console.assert(a === 1);
    console.assert(b === 2);
    console.assert(c === 30);

    const [x = 99] = [];
    console.assert(x === 99);

    // Object destructure with rename + default, empty source object.
    const { a: ra = 99 } = { } as { a?: number };
    console.assert(ra === 99);
}
main();
