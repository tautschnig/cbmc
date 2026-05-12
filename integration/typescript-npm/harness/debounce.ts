// Harness for npm package 'debounce' (sindresorhus/debounce)
// Version tracked in integration/typescript-npm/package.json
//
// debounce(fn, wait) returns a wrapper that defers fn until `wait`
// milliseconds have elapsed since the last call. We reimplement the
// state-tracking logic and verify:
//  - Multiple rapid calls within `wait` only invoke the underlying
//    function once (the last one, by call order).
//  - The cached value reflects the most recent call's argument.
//
// Upstream source (simplified):
//   function debounce(fn, wait) {
//     let timeout, lastArg;
//     return function(...args) {
//       lastArg = args[args.length - 1];
//       clearTimeout(timeout);
//       timeout = setTimeout(() => fn(lastArg), wait);
//     };
//   }
//
// We model time as discrete "ticks" and check invariants over a
// bounded sequence of calls.

class Debouncer {
  pending: boolean;
  lastArg: number;
  callCount: number;      // number of times the WRAPPER was called
  invokeCount: number;    // number of times the UNDERLYING fn was called

  constructor() {
    this.pending = false;
    this.lastArg = 0;
    this.callCount = 0;
    this.invokeCount = 0;
  }

  // Called on each external trigger; resets the timer
  trigger(arg: number): void {
    this.callCount = this.callCount + 1;
    this.lastArg = arg;
    this.pending = true;
  }

  // Called when the timer fires (simulated)
  fire(): number {
    if (!this.pending) return this.lastArg;
    this.invokeCount = this.invokeCount + 1;
    this.pending = false;
    return this.lastArg;
  }
}

// Property 1: firing after N triggers invokes fn exactly once.
const d = new Debouncer();
const a: number = nondet_number();
const b: number = nondet_number();
const c: number = nondet_number();
__CPROVER_assume(!Number.isNaN(a) && a >= -100 && a <= 100);
__CPROVER_assume(!Number.isNaN(b) && b >= -100 && b <= 100);
__CPROVER_assume(!Number.isNaN(c) && c >= -100 && c <= 100);
d.trigger(a);
d.trigger(b);
d.trigger(c);
console.assert(d.callCount === 3);
console.assert(d.invokeCount === 0);
d.fire();
console.assert(d.invokeCount === 1);

// Property 2: fired-value matches last trigger argument.
console.assert(d.fire() === c);

// Property 3: after firing, firing again without new trigger is a no-op.
const before: number = d.invokeCount;
d.fire();
console.assert(d.invokeCount === before);

// Property 4: fresh Debouncer has invokeCount 0.
const d2 = new Debouncer();
console.assert(d2.invokeCount === 0);
console.assert(d2.callCount === 0);
