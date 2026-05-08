// ES2024 §24.2: Set dedup + delete + clear
const s: Set<number> = new Set();
s.add(1);
s.add(2);
s.add(3);
console.assert(s.size === 3);

// ES2024 §24.2.3.1: add is idempotent
s.add(1);
console.assert(s.size === 3);      // no duplicate
s.add(2);
console.assert(s.size === 3);

// delete
s.delete(2);
console.assert(s.size === 2);
console.assert(!s.has(2));
console.assert(s.has(1));
console.assert(s.has(3));

// delete non-existent
s.delete(99);
console.assert(s.size === 2);

// clear
s.clear();
console.assert(s.size === 0);
console.assert(!s.has(1));
