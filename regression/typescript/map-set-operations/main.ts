const m = new Map<string, number>();
m.set("a", 1);
m.set("b", 2);
m.set("c", 3);
console.assert(m.size === 3);
m.delete("b");
console.assert(m.size === 2);

const s = new Set<number>();
s.add(10);
s.add(20);
s.add(30);
console.assert(s.size === 3);
