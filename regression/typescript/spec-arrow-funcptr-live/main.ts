// Regression for: ES2024 §8.1 / §15.3 live-binding capture through a
// function pointer. `const f = foo` creates a function-pointer alias;
// `f()` must dispatch to foo. Prior frontend (a) skipped converting
// functions that were referenced only by name (not called), and (b)
// typecast the code-typed symbol to code* instead of taking its
// address, leaving the value undefined at symex.
function foo(): number { return 42; }
function outer(): () => number {
    let x = 10;
    const inner = () => x;   // captures `x` as a live binding
    x = 20;                  // modified BEFORE return
    return inner;            // spec: inner() should see x = 20
}
function main(): void {
    const f = foo;
    console.assert(f() === 42);

    const g = outer();
    console.assert(g() === 20);
}
main();
