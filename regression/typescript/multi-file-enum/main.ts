import { Color, isRed } from "./colors";
console.assert(isRed(Color.Red) === true);
console.assert(isRed(Color.Blue) === false);
