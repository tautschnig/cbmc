class C { x: number; constructor(v: number) { this.x = v; } add(n: number): number { return this.x + n; } } const c = new C(10); console.assert(c.add(5) === 15);
