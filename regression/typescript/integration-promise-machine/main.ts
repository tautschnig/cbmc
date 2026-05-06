// Promise-like state machine (simplified)
enum PromiseState { Pending = 0, Fulfilled = 1, Rejected = 2 }

class SimplePromise<T> {
  state: PromiseState;
  value: T;
  errorCode: number;

  constructor(init: T) {
    this.state = PromiseState.Pending;
    this.value = init;
    this.errorCode = 0;
  }

  resolve(v: T): void {
    if (this.state === PromiseState.Pending) {
      this.state = PromiseState.Fulfilled;
      this.value = v;
    }
  }

  reject(code: number): void {
    if (this.state === PromiseState.Pending) {
      this.state = PromiseState.Rejected;
      this.errorCode = code;
    }
  }

  isSettled(): boolean {
    return this.state !== PromiseState.Pending;
  }
}

const p = new SimplePromise<number>(0);
console.assert(p.state === PromiseState.Pending);
console.assert(p.isSettled() === false);
p.resolve(42);
console.assert(p.state === PromiseState.Fulfilled);
console.assert(p.value === 42);
console.assert(p.isSettled() === true);
p.reject(1); // should be no-op
console.assert(p.state === PromiseState.Fulfilled);
console.assert(p.errorCode === 0);
