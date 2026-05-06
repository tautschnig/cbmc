const prices = new Map<string, number>();
prices.set("apple", 100);
prices.set("banana", 50);
prices.set("cherry", 200);
console.assert(prices.get("apple") === 100);
console.assert(prices.get("banana") === 50);
console.assert(prices.size === 3);
