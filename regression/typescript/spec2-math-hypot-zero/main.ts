// Regression for: ES2024 §21.3.2.17 Math.hypot() with zero args.
// Prior implementation required args.size() >= 1; zero-arg case fell
// through to nondet.
function main(): void {
    console.assert(Math.hypot() === 0);
    console.assert(Math.hypot(5) === 5);
    console.assert(Math.hypot(3, 4) === 5);
    console.assert(Math.hypot(6, 8) === 10);
}
main();
