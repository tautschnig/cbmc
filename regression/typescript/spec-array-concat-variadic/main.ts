// Regression for: ES2024 §23.1.3.2 Array.prototype.concat. Must
// accept any number of args; arrays spread one level, scalars push
// as-is. Prior implementation only considered the first arg and only
// if it was an array, so `[1,2].concat(3, 4)` produced length 1.
function main(): void {
    const a = [1, 2];
    const b = a.concat(3, 4);
    console.assert(b.length === 4);
    console.assert(b[0] === 1);
    console.assert(b[1] === 2);
    console.assert(b[2] === 3);
    console.assert(b[3] === 4);
    const c = [1].concat([2, 3]);
    console.assert(c.length === 3);
    console.assert(c[2] === 3);
}
main();
