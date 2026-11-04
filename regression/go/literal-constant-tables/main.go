// Generated tables: array and slice literals of integer and boolean
// constants (sent by the helper as their values), and constant expressions
// such as long concatenations of string literals (sent as their value).
package main

import "cprover"

var table = [300]uint8{0, 138, 25, 163, 50, 188, 75, 213, 100, 238, 125, 12, 150, 37, 175, 62, 200, 87, 225, 112, 250, 137, 24, 162, 49, 187, 74, 212, 99, 237, 124, 11, 149, 36, 174, 61, 199, 86, 224, 111, 249, 136, 23, 161, 48, 186, 73, 211, 98, 236, 123, 10, 148, 35, 173, 60, 198, 85, 223, 110, 248, 135, 22, 160, 47, 185, 72, 210, 97, 235, 122, 9, 147, 34, 172, 59, 197, 84, 222, 109, 247, 134, 21, 159, 46, 184, 71, 209, 96, 234, 121, 8, 146, 33, 171, 58, 196, 83, 221, 108, 246, 133, 20, 158, 45, 183, 70, 208, 95, 233, 120, 7, 145, 32, 170, 57, 195, 82, 220, 107, 245, 132, 19, 157, 44, 182, 69, 207, 94, 232, 119, 6, 144, 31, 169, 56, 194, 81, 219, 106, 244, 131, 18, 156, 43, 181, 68, 206, 93, 231, 118, 5, 143, 30, 168, 55, 193, 80, 218, 105, 243, 130, 17, 155, 42, 180, 67, 205, 92, 230, 117, 4, 142, 29, 167, 54, 192, 79, 217, 104, 242, 129, 16, 154, 41, 179, 66, 204, 91, 229, 116, 3, 141, 28, 166, 53, 191, 78, 216, 103, 241, 128, 15, 153, 40, 178, 65, 203, 90, 228, 115, 2, 140, 27, 165, 52, 190, 77, 215, 102, 240, 127, 14, 152, 39, 177, 64, 202, 89, 227, 114, 1, 139, 26, 164, 51, 189, 76, 214, 101, 239, 126, 13, 151, 38, 176, 63, 201, 88, 226, 113, 0, 138, 25, 163, 50, 188, 75, 213, 100, 238, 125, 12, 150, 37, 175, 62, 200, 87, 225, 112, 250, 137, 24, 162, 49, 187, 74, 212, 99, 237, 124, 11, 149, 36, 174, 61, 199, 86, 224, 111, 249, 136, 23, 161, 48, 186, 73, 211, 98}

var sparse = []int16{5: 1, 20: -3, 100: 7, 0: 2, 1: 3, 2: 4, 3: 5, 4: 6, 6: 8, 7: 9, 8: 10, 9: 11, 10: 12, 11: 13, 12: 14, 13: 15}

var flags = [...]bool{true, false, false, true, false, false, true, false, false, true, false, false, true, false, false, true, false, false, true, false, false, true, false, false, true, false, false, true, false, false, true, false, false, true, false, false, true, false, false, true}

const names = "alpha" + "beta" + ("gamma" + "delta")

const mask = 1<<10 | 0x0f&^3

func main() {
	cprover.Assert(names == "alphabetagammadelta" && len(names) == 19, "concatenation")
	cprover.Assert(mask == 1036, "constant arithmetic")
	cprover.Assert(table[0] == 0 && table[1] == 138 && table[299] == 98, "array")
	cprover.Assert(len(sparse) == 101 && sparse[100] == 7 && sparse[50] == 0 && sparse[20] == -3, "sparse slice")
	cprover.Assert(flags[0] && !flags[1] && flags[39], "bools")
	sparse[50] = 9
	cprover.Assert(sparse[50] == 9, "mutable")
}
