export class Counter {
  count: number;
  constructor() { this.count = 0; }
  increment(): void { this.count = this.count + 1; }
  getCount(): number { return this.count; }
}
