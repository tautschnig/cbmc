const m = new Map<string, number>();
m.set("a", 10); m.set("b", 20); m.set("c", 30);
let total: number = 0;
for (const [k, v] of m) { total = total + v; }
console.assert(total === 60);
