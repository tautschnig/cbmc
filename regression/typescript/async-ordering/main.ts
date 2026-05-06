// This test documents that true asynchronous execution ordering
// is NOT modeled. In real JS, async operations can interleave.
// Our model treats everything as synchronous (sequential).
let order: number = 0;
async function first(): Promise<void> { order = 1; }
async function second(): Promise<void> { order = 2; }
// In true async, the order of completion is nondeterministic.
// In our sync model, it's always sequential.
await first();
await second();
console.assert(order === 2); // Always true in sync model
// A true async model would need to verify for ALL interleavings
