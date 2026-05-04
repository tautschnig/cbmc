async function fetchValue(): Promise<number> { return 42; }
const val: number = await fetchValue();
console.assert(val === 42);
