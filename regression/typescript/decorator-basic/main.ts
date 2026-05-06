function log(target: any, key: string, descriptor: any): any {
  return descriptor;
}

class Calculator {
  @log
  add(x: number, y: number): number { return x + y; }

  @log
  multiply(x: number, y: number): number { return x * y; }
}

const c = new Calculator();
console.assert(c.add(2, 3) === 5);
console.assert(c.multiply(4, 5) === 20);
