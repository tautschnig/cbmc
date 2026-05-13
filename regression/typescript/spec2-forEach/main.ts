// Regression for: ES2024 §23.1.3.12 Array.prototype.forEach.
// Calls the callback with (value, index, array) for each element.
// Prior implementation was missing entirely — fell through to nondet
// so side effects were discarded.
function main(): void {
    const a = [10, 20, 30];
    let sum = 0;
    a.forEach((v) => { sum += v; });
    console.assert(sum === 60);

    // With index
    let indexed = 0;
    a.forEach((v, i) => { indexed += v * i; });
    console.assert(indexed === 80);

    // Empty array — callback never runs
    let count = 0;
    const empty: number[] = [];
    empty.forEach(() => { count++; });
    console.assert(count === 0);
}
main();
