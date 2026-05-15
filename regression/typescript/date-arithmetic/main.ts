// ES2024 §21.4: Date arithmetic and comparison.
function main(): void {
    const d1 = new Date(2000);
    const d2 = new Date(1000);

    // Subtraction via getTime() gives ms difference
    const diff: number = d1.getTime() - d2.getTime();
    console.assert(diff === 1000);

    // Comparison via getTime()
    console.assert(d1.getTime() > d2.getTime());
    console.assert(d2.getTime() < d1.getTime());

    // Ordering property: later timestamp is greater
    const d3 = new Date(5000);
    console.assert(d3.getTime() > d1.getTime());
    console.assert(d3.getTime() - d1.getTime() === 3000);
}
main();
