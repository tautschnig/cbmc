function add(a: number, b: number): number { return a + b; }
function addThree(a: number, b: number, c: number): number { return add(add(a, b), c); }
console.assert(addThree(1, 2, 3) === 6);
