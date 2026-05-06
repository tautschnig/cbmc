const m = new Map<string, number>();
m.set("x", 42);
const val: number = m.get("x") as number;
console.assert(val === 42);
