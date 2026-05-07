// Simplified classnames
function cls(arr: string[]): string {
  if (arr.length === 0) return "";
  if (arr.length === 1) return arr[0];
  let result: string = arr[0];
  for (let i = 1; i < arr.length; i++) {
    result = result + " " + arr[i];
  }
  return result;
}
const single: string[] = ["button"];
console.assert(cls(single) === "button");
