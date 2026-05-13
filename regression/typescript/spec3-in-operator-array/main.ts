// Regression for: ES2024 §13.10.2 in-operator on arrays. Prior
// implementation only handled string-keyed objects. For
// `i in arr`, TS's binary-expression type promotion was casting
// the array struct to floatbv, defeating the in-operator's struct
// dispatch. Fix: skip type promotion for InKeyword.
function main(): void {
    const a = [10, 20, 30];
    console.assert(0 in a);
    console.assert(1 in a);
    console.assert(2 in a);
    console.assert(!(3 in a));     // past length
    console.assert(!(-1 in a));    // negative

    // Object form still works.
    const o = { x: 1, y: 2 };
    console.assert("x" in o);
    console.assert(!("z" in o));
}
main();
