// ES2024 §20.1.2.2: Object.create(proto[, descriptors]).
//
// We model two practical cases:
//   - Object.create(Class.prototype) → fresh struct of Class type
//     (no constructor call). Property accesses then resolve in
//     the static struct shape.
//   - Object.create(null) → empty struct ("safe map" idiom).
//
// Anything more dynamic (Object.create(someInstance)) clones the
// struct; chains are NOT modelled — see typescript-known-limitations
// §1.6.

class Animal {
    public name: string = "default";
    public legs: number = 4;
}

function main(): void {
    // Case (a): Object.create(Class.prototype) gives a Class-shaped
    // object whose fields are bound on first assignment.
    const a = Object.create(Animal.prototype);
    a.legs = 10;
    a.name = "Rex";
    console.assert(a.legs === 10);
    console.assert(a.name === "Rex");

    // Independent allocations don't alias — write to one, read
    // from the other.
    const b = Object.create(Animal.prototype);
    b.legs = 99;
    console.assert(a.legs === 10);
    console.assert(b.legs === 99);

    // Case (b): Object.create(null) gives an empty struct.
    // The result has no properties; we just check that the
    // expression doesn't crash.
    const safeMap = Object.create(null);
    console.assert(true);
}
main();
