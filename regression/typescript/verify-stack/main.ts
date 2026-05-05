class Stack {
  data: number[];
  top: number;
  constructor() { this.data = []; this.top = 0; }
  push(x: number): void {
    this.data.push(x);
    this.top = this.top + 1;
  }
  size(): number { return this.top; }
}
const s = new Stack();
s.push(10);
s.push(20);
s.push(30);
console.assert(s.size() === 3);
