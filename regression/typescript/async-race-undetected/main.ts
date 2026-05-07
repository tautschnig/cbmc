// Race condition: two async tasks concurrently write to shared state.
// Under true async semantics, the final value is nondeterministic.
// Under our sequential model, source order determines the result.
//
// This test asserts the SEQUENTIAL-model result (flag === 2) to expose
// that we miss the race: a sound verifier should FAIL this test because
// flag could also be 1 under valid interleavings.

let flag: number = 0;
async function taskA(): Promise<void> { flag = 1; }
async function taskB(): Promise<void> { flag = 2; }

const pa: Promise<void> = taskA();
const pb: Promise<void> = taskB();
await pa;
await pb;

// Sequential: always 2. Sound async: could be 1 OR 2, so this fails.
console.assert(flag === 2);
