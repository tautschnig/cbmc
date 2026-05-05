class Stack {
  top: number;
  data0: number;
  data1: number;
  data2: number;
  constructor() { this.top = 0; this.data0 = 0; this.data1 = 0; this.data2 = 0; }
  push(v: number): void {
    if (this.top === 0) this.data0 = v;
    else if (this.top === 1) this.data1 = v;
    else this.data2 = v;
    this.top = this.top + 1;
  }
  peek(): number {
    if (this.top === 1) return this.data0;
    if (this.top === 2) return this.data1;
    return this.data2;
  }
  size(): number { return this.top; }
}
const s = new Stack();
s.push(10);
s.push(20);
console.assert(s.size() === 2);
console.assert(s.peek() === 20);
