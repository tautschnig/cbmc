// ES2024 §27.5: Generator functions with yield.
function* counter(): Generator<number> {
    yield 1;
    yield 2;
    yield 3;
}
function main(): void {
    const gen = counter();
    console.assert(gen.next().value === 1);
    console.assert(gen.next().value === 2);
    console.assert(gen.next().value === 3);
    console.assert(gen.next().done === true);
}
main();
