let counter: number = 0;
function increment(): void { counter = counter + 1; }
increment();
increment();
increment();
console.assert(counter === 3);
