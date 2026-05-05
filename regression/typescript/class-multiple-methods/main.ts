class Stack {
  items: number[];
  size: number;
  constructor() { this.items = []; this.size = 0; }
  push(x: number): void { this.items.push(x); this.size = this.size + 1; }
  getSize(): number { return this.size; }
}
const s = new Stack();
s.push(1);
s.push(2);
console.assert(s.getSize() === 2);
