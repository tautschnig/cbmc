// ES2024 §21.4: Date type basic operations.
function main(): void {
    // new Date(ms) — specific timestamp
    const d = new Date(1700000000000);
    console.assert(d.getTime() === 1700000000000);
    console.assert(d.valueOf() === 1700000000000);

    // new Date() — nondet but >= 0
    const current = new Date();
    console.assert(current.getTime() >= 0);

    // Date.now() — nondet >= 0
    const now = Date.now();
    console.assert(now >= 0);
}
main();
