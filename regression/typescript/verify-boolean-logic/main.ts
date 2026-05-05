const a: boolean = nondet_boolean();
const b: boolean = nondet_boolean();
// De Morgan's law
console.assert(!(a && b) === (!a || !b));
console.assert(!(a || b) === (!a && !b));
