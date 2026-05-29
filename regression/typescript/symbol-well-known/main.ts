// ES2024 §6.1.5.1: well-known Symbols (Symbol.iterator,
// Symbol.toPrimitive, etc.) modelled as named special properties
// `@@<name>`. Class methods declared with computed property name
// `[Symbol.X]() { ... }` register as `@@X`; element access of the
// form `obj[Symbol.X]` is rewritten at parse time to property
// access `obj.@@X`.

class Counter {
    [Symbol.iterator](): number {
        return 42;
    }
}

class Stringy {
    [Symbol.toPrimitive](hint: string): number {
        return hint === "number" ? 100 : 200;
    }
}

class MultiSym {
    [Symbol.iterator](): number { return 1; }
    [Symbol.toPrimitive](): number { return 2; }
    [Symbol.asyncIterator](): number { return 3; }
}

function main(): void {
    const c = new Counter();
    console.assert(c[Symbol.iterator]() === 42);

    const s = new Stringy();
    console.assert(s[Symbol.toPrimitive]("number") === 100);
    console.assert(s[Symbol.toPrimitive]("default") === 200);

    const m = new MultiSym();
    console.assert(m[Symbol.iterator]() === 1);
    console.assert(m[Symbol.toPrimitive]() === 2);
    console.assert(m[Symbol.asyncIterator]() === 3);
}
main();
