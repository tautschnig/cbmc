// Regression test for §2.9 of typescript-known-limitations.md.
// `for..of` over a method-call iterable, with `map[k] = map[k] || {}`
// inside the body, currently trips a member_exprt invariant.

const f = (data: string) => {
  const map: { [k: string]: any } = {};
  for (const k of data.split("\n")) {
    map[k] = map[k] || {};
  }
};
