const a: boolean = true;
const b: boolean = false;
const c: boolean = a && !b;
console.assert(c === true);
const d: boolean = !a || b;
console.assert(d === false);
