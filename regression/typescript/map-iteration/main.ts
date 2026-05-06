const m = new Map<string, number>();
m.set("x", 10);
m.set("y", 20);
m.set("z", 30);
let sum: number = 0;
for (const [key, value] of m) {
  sum += value;
}
console.assert(sum === 60);
