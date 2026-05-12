// KNOWNBUG: nested loop where the inner loop reads a growing
// array's .length triggers spurious bounds violations. This was
// found via the array-union integration harness:
//   for i in src:
//     for j in 0..result.length:    // result.length grows via push
//       ...result[j]...              // CBMC flags out-of-bounds
//     if not seen: result.push(src[i])
// The bounds checker doesn't correctly relate the loop bound
// (result.length) to the index (j).
const src: number[] = [1, 2];
const result: number[] = [];
for (let i = 0; i < src.length; i++) {
  let seen: boolean = false;
  for (let j = 0; j < result.length; j++) {
    if (result[j] === src[i]) {
      seen = true;
    }
  }
  if (!seen) {
    result.push(src[i]);
  }
}
console.assert(result.length === 2);
