// Harness for npm package 'once' (isaacs/once)
// Version tracked in integration/typescript-npm/package.json
//
// once(fn) returns a wrapped function that invokes fn exactly once
// and returns the cached result on subsequent calls. We verify
// idempotence and state-tracking invariants over SYMBOLIC call counts.
//
// Upstream: https://github.com/isaacs/once/blob/v1.4.0/once.js

// Reimplementation: track call count and cached return value.
class OnceWrap {
  callCount: number;
  cachedValue: number;
  inputValue: number;
  constructor(input: number) {
    this.callCount = 0;
    this.cachedValue = 0;
    this.inputValue = input;
  }
  invoke(): number {
    if (this.callCount >= 1) {
      return this.cachedValue;
    }
    this.callCount = 1;
    // Simulated work: cache some function of the input.
    this.cachedValue = this.inputValue * 2;
    return this.cachedValue;
  }
  getCallCount(): number {
    return this.callCount;
  }
}

// Property 1: idempotence. Multiple calls return the same value.
const input: number = nondet_number();
__CPROVER_assume(!Number.isNaN(input) && input >= -1000 && input <= 1000);

const w1 = new OnceWrap(input);
const r1: number = w1.invoke();
const r2: number = w1.invoke();
const r3: number = w1.invoke();
console.assert(r1 === r2);
console.assert(r2 === r3);

// Property 2: call-count tracking. After N >= 1 invocations, count is 1.
console.assert(w1.getCallCount() === 1);

// Property 3: cached value matches what the underlying function would return
// on the first call (input * 2 in this reimplementation).
console.assert(r1 === input * 2);

// Property 4: a second wrapper has independent state.
const w2 = new OnceWrap(input + 10);
const s1: number = w2.invoke();
console.assert(s1 === (input + 10) * 2);
// Calling w2 doesn't affect w1's cached value.
console.assert(w1.invoke() === input * 2);
console.assert(w2.invoke() === (input + 10) * 2);

// Property 5: never-invoked wrapper has callCount === 0.
const w3 = new OnceWrap(input);
console.assert(w3.getCallCount() === 0);
