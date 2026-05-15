// ES2024 §28.2.1: Proxy — pragmatic model returns target unchanged.
function main(): void {
    const target = { x: 42, y: 7 };
    const proxy = new Proxy(target, {});
    console.assert(proxy.x === 42);
    console.assert(proxy.y === 7);
}
main();
