// Regression for: ES2024 §23.1.3.12 Array.prototype.includes with
// fromIndex. Negative fromIndex adds length and clamps to [0,
// length]; prior implementation ignored the fromIndex argument.
function main(): void {
    const a = [1, 2, 3];
    console.assert(a.includes(2));
    console.assert(!a.includes(4));
    console.assert(a.includes(1, 0));
    console.assert(!a.includes(1, 1));    // fromIndex past element 1
    console.assert(a.includes(3, -1));    // fromIndex = 2
}
main();
