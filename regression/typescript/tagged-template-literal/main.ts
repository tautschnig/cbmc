// ES2024 §13.3.11: Tagged template expressions.
// tag`head${expr}tail` calls tag with the template parts and values.
// For constant interpolations, we fold at conversion time.
function tag(strings: TemplateStringsArray, ...values: number[]): string {
    let result = "";
    for (let i = 0; i < strings.length; i++) {
        result += strings[i];
        if (i < values.length) result += String(values[i]);
    }
    return result;
}

function main(): void {
    const x = 42;
    const s = tag`value is ${x}!`;
    console.assert(s === "value is 42!");

    // No substitution
    const plain = tag`hello`;
    console.assert(plain === "hello");
}
main();
