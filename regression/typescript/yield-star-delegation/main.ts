// ES2024 §27.5: `yield*` delegation, constant case.
//
// When a generator's body contains `yield* otherGen()` and `otherGen`
// is a generator whose yield values are known at conversion time,
// the delegated yields are inlined into the outer generator's
// values list. So the consumer sees a flat sequence of yields from
// outer's own + the delegate's + outer's remaining.

function* inner(): Generator<number> {
    yield 10;
    yield 20;
}

function* outer(): Generator<number> {
    yield 1;
    yield* inner();
    yield 99;
}

function main(): void {
    const gen = outer();
    console.assert(gen.next().value === 1);
    console.assert(gen.next().value === 10);
    console.assert(gen.next().value === 20);
    console.assert(gen.next().value === 99);
    console.assert(gen.next().done === true);
}
main();
