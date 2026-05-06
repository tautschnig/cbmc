const s = new Set<number>();
s.add(5); s.add(10); s.add(15);
let total: number = 0;
for (const item of s) {
  total += item;
}
console.assert(total === 30);
