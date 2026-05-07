// Same test as async-race-undetected but with --ts-async-threading.
// With threading, CBMC detects the race: flag could be 1 OR 2,
// so asserting flag === 2 must fail in at least one interleaving.
let flag: number = 0;
async function taskA(): Promise<void> { flag = 1; }
async function taskB(): Promise<void> { flag = 2; }

const pa: Promise<void> = taskA();
const pb: Promise<void> = taskB();
await pa;
await pb;

// Under threading: FAILS (flag could be 1 under some interleaving)
console.assert(flag === 2);
