// 500 CORE milestone — generic stack
class GenericStack<T> {
  items: T[];
  top: number;
  constructor() { this.items = []; this.top = 0; }
  push(v: T): void { this.items[this.top] = v; this.top = this.top + 1; }
  peek(): T { return this.items[this.top - 1]; }
  size(): number { return this.top; }
}
const s = new GenericStack<number>();
s.push(10); s.push(20); s.push(30);
console.assert(s.size() === 3);
console.assert(s.peek() === 30);
