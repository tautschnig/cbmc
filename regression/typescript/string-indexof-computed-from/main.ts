// ES2024 §22.1.3.9: indexOf with computed fromIndex.
function main(): void {
    const s = "a.b.c";
    const first = s.indexOf(".");
    console.assert(first === 1);
    const second = s.indexOf(".", first + 1);
    console.assert(second === 3);
    // No more dots after position 4
    const third = s.indexOf(".", second + 1);
    console.assert(third === -1);
}
main();
