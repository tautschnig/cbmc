// Regression for: heterogeneous tuple destructuring
// `const [a, b] = tuple`. Prior implementation hit the array-
// destructure path which reads from `data[i]`, but our tuple
// representation uses named components `_0`, `_1`, .... The
// destructure now detects typescript_tuple struct and reads each
// component by name.
function main(): void {
    // Two-element tuple
    const [a, b]: [number, string] = [1, "x"];
    console.assert(a === 1);
    console.assert(b === "x");

    // Three-element tuple with heterogeneous types
    const t: [number, boolean, string] = [42, true, "hello"];
    const [n, flag, msg] = t;
    console.assert(n === 42);
    console.assert(flag === true);
    console.assert(msg === "hello");

    // Ignored binding (skip element)
    function pair(): [number, string] { return [10, "skipped"]; }
    const [x] = pair();
    console.assert(x === 10);
}
main();
