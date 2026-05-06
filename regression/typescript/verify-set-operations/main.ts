const s = new Set<number>();
s.add(1); s.add(2); s.add(3);
console.assert(s.size === 3);
s.delete(2);
console.assert(s.size === 2);
