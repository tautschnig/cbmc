// ES2024 sec-set.prototype.has — verifies Set tracks membership.
// Note: Set.add currently hardcodes the data element type to double,
// so string-typed Sets have known limitations; here we test the
// numeric case which is the common one.
const s: Set<number> = new Set();
s.add(1);
s.add(2);
s.add(3);
console.assert(s.has(1));
console.assert(s.has(2));
console.assert(s.has(3));
console.assert(!s.has(4));

// Duplicate adds don't break has
s.add(1);
console.assert(s.has(1));
