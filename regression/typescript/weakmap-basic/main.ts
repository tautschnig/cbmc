// ES2024 §24.3: WeakMap — modelled as Map (no GC in BMC).
function main(): void {
    const wm = new WeakMap();
    const key = { id: 1 };
    wm.set(key, "value");
    console.assert(wm.has(key));
    console.assert(wm.size === 1);
}
main();
