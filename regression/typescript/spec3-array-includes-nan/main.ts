// Regression for: ES2024 §23.1.3.12 Array.prototype.includes uses
// SameValueZero, which treats NaN === NaN as true. Prior
// implementation used IEEE strict equality, returning false for
// NaN-in-array checks (under-approximation).
function main(): void {
    const a = [1, NaN, 3];
    console.assert(a.includes(NaN));   // SameValueZero finds NaN
    console.assert(a.includes(1));
    console.assert(a.includes(3));
    console.assert(!a.includes(5));

    // Empty array returns false
    const empty: number[] = [];
    console.assert(!empty.includes(NaN));
}
main();
