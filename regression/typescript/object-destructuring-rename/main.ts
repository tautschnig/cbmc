// TSH: Destructuring > Property Renaming
const obj = { a: 1, b: 2, c: 3 };

// Plain destructuring
const { a } = obj;
console.assert(a === 1);

// With rename
const { b: renamedB } = obj;
console.assert(renamedB === 2);

// Multiple with mix
const { a: first, b: second } = obj;
console.assert(first === 1);
console.assert(second === 2);
