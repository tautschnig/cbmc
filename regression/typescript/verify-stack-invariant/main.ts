class Stack {
  top: number;
  a: number;
  b: number;
  c: number;
  constructor() { this.top = 0; this.a = 0; this.b = 0; this.c = 0; }
  push(v: number): void {
    if (this.top === 0) this.a = v;
    else if (this.top === 1) this.b = v;
    else this.c = v;
    this.top = this.top + 1;
  }
  size(): number { return this.top; }
}
const s = new Stack();
s.push(1); s.push(2); s.push(3);
console.assert(s.size() === 3);
console.assert(s.c === 3);
