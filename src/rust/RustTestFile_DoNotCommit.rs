
fn main() {
  /*test_0();
  test_1();
  test_2();
  test_3();
  test_4();
  test_5();
  test_6();
  test_7();
  test_8();
  test_9();
  test_10();
  test_11();
  test_12();
  test_13();
  test_14();
  test_15();
  test_16();
  test_17();
  test_18();
  test_19();
  test_20();
  test_22();*/
  test_23();
  /*test_24();
  test_25();*/
}

/*// First Test
fn test_0() {
  let x : u32;
  let y = x * 2;
  assert!(y % 2 == 0);
}

// Equal and Not Equal
fn test_1() {
  let x : u32;
  let y = x * 2;

  assert!(y % 2 == 0);
  assert!(y % 2 != 3);
}

// LT, GT, LE, GE
fn test_2() {
  let a : u32;
  let b = a / 2;
  assert!(b <= a);
  let c = b * 2;
  assert!(c >= b);
  let d = b + 5;
  let e = d + 1;
  assert!(e > d);
  let f = e - 2;
  assert!(f < d);
}

// +, -, *, /
fn test_3() {
  let a : u32;
  assert!(            a / 2 <= a        );
  assert!(        a / 2 * 2 >= a / 2    );
  assert!(    a / 2 + 5 + 1 >  a / 2 + 5);
  assert!(a / 2 + 5 + 1 - 2 <  a / 2 + 5);
}

// Simple Scopes
fn test_4() {
  let a : u32;
  let b = a / 2;
  let c = a / 2;
  {
    let c = c + 1;
    assert!(c > b);
  }
  assert!(c == b);
}

// Non-Returning If Else Statements
fn test_5() {
  let a : u32;

  if a % 3 == 0 {
    assert!(a != 4);
  }
  
  if a % 3 == 0 {
    assert!(a != 5);
  }
  else if a % 3 == 1 {
    assert!(a > 0);
  }
  else {
    assert!(a > 1);
  }
}

// i32, Unary-
fn test_6() {
  let a : i32;
  let b = -a;

  if a > 0 {
    assert!(a > b);
  }
  else if a < 0 {
    assert!(a < b - 1);
  }
  else {
    assert!(b == a);
  }
}

// Bitwise |, &, ^, !
fn test_7() {
  assert!(3 | 5 == 7);
  assert!(7 & 9 == 1);
  assert!(9 ^ 7 == 14);
  assert!(!8 ^ !0 == 8);

  let x = 1;
  let a : u32;
  let b : u32;
  let c = a + b;
  if c & x == x {
    let d = a ^ b;
    assert!(d & x == x);
  }
  else {
    assert!(a & x == b & x)
  }
}

// true, false, Boolean &&, ||, !
fn test_8() {
  assert!(true);
  assert!(true || false);
  assert!(!false);

  let a = true;
  let b = false;
  let c = a || b;
  let d = c && a;
  assert!(d && true);
  assert!(!b && d);

  let e : bool;
  assert!(e || !e);
}

// Non-returning Loop Loop, Mut
fn test_9() {
  let mut a : u32;
  
  loop {
     a = a / 2;
     
     if a == 0 {
        break;
     }
  }
  
  assert!(a == 0);
}

// Non-returning While Loop
fn test_10() {
  let mut a : u32;

  while a > 0 {
    a = a / 2;
  }

  assert!(a == 0);
}

// +=, -=, *=, /=
fn test_11() {
  let mut a : u32;
  a /= 2;
  let mut b : u32;
  b /= 2;
  let c = b;
  b += a;

  let d = a;

  assert!(b > a || a == 0 || c == 0);

  b -= a;

  assert!(c == b);

  a *= 2;

  assert!(a > d || d == 0);
}

// %=, &=, |=, ^=
fn test_12() {
  let mut x = 0;
  x |= 1;
  assert!(x == 1);
  x ^= 7;
  assert!(x == 6);
  x %= 4;
  assert!(x == 2);
  x = 18;
  x &= 15;
  assert!(x == 2);

  let mut a : u32;
  a %= 8;

  let mut b : u32;
  b %= 8;

  let mut c = a;
  let mut d = b;

  c &= b;
  d |= a;

  assert!(c < 8 && d < 8);
  assert!(c + d >= c & d);
}

// <<, >>, <<=, >>=
fn test_13() {
  assert!(8 >> 4 == 0);
  assert!(1 << 4 == 16);

  let mut a = 8;
  assert!(a >> 1 == 4);
  assert!(a << 1 == 16);

  a <<= 2;
  assert!(a == 32);
  a >>= 3;
  assert!(a == 4);
}

// Returning code blocks
fn test_14() {
  let x = {
    5
  };
  assert!(x == 5);

  let a = {
    let mut b = 3;
    b *= 3;
    b
  };
  assert!(a == 9);

  let c = {
    let mut c = 3;
    c *= 3;
    c + 1
  };
  assert!(c == 10);

  let d : u32;
  let e = {
    let f : u32;

    if d < 10 {
      f = d;
    } else {
      f = 10;
    }

    f
  };
  assert!(e == d || e == 10);

  let g : u32;
  let h = {
    if g < 10 {
      g
    } else {
      10
    }
  };
  assert!(h == g || h == 10);
}

// Returning if else statements
fn test_15() {
  let a : u32;

  let b =
  if a < 10 {
    a
  } else {
    10
  };

  assert!(b == a || b == 10);
}

// Returning if elseif else statements
fn test_16() {
  let a : u32;

  let b =
  if a % 3 == 0 {
    assert!(a != 5);
    0
  }
  else if a % 3 == 1 {
    assert!(a > 0);
    1
  }
  else {
    assert!(a > 1);
    2
  };

  assert!(b < 3);

  let c : u32;

  let d =
  if c > 100 {
    c
  } else if c < 10 {
    c + 101
  } else {
    c * 11
  };

  assert!(d > 100);
}

// Parentheses
fn test_17() {
  let a = 10;
  let b = (a + 6) / 2;
  assert!(b == 8);
  let c = b * (a + 1);
  assert!(c == 88);
  
  let d = 55;
  let e = (b * (d + 8) + 1) * a;
  assert!(e == 10 * (500 + 5));
}

// f32
fn test_18() {
  assert!(1.1 == 1.1 * 1.0);
  assert!(1.1 != 1.11 / 1.0);

  let a : f32;
  let b = a / 2.0;
  
  if a < 0.0 {
    assert!(a <= b);
  } else if a >= 0.0 {
    assert!(a >= b);
  }

  let c = b * 2.0;
  // general/infinity            Close but not exact                    NAN
  assert!(a == c || a - c < 0.00000001 || c - a < 0.00000001 || c * 0.0 != 0.0);

  let d : f32 = 0.0;
  assert!(d + 1.0 > 0.0);
  assert!(d - 1.0 < 0.0);
}

// f64
fn test_19() {
  let a : f64;
  let b = a / 2.0;
  
  if a < 0.0 {
    assert!(a <= b);
  } else if a >= 0.0 {
    assert!(a >= b);
  }

  let c = b * 2.0;
  // general/infinity            Close but not exact                    NAN
  assert!(a == c || a - c < 0.00000001 || c - a < 0.00000001 || c * 0.0 != 0.0);

  let d : f64 = 0.0;
  assert!(d + 1.0 > 0.0);
  assert!(d - 1.0 < 0.0);
}

// Function Calls -- no return, no parameters
fn test_20() {
  assert!(true);
  trivial_function();
  assert!(1.0 == 1.0);
}
fn trivial_function() {
  assert!(1 + 1 == 2);
}

// Function Calls -- return values
fn test_21() {
  let a = return_u32();
  assert!(a < 10);
  assert!(return_u32() < 10);
  assert!(return_u64() >= 100);
  assert!(return_bool());
  assert!(return_f32() < 21.0);
  assert!(return_f64() < 11.0 && return_f64() > -11.0);
}
fn return_u32() -> u32 {
  let x : u32;

  if x < 10 {
    return x;
  } else {
    return 5;
  }
}
fn return_u64() -> u64 {
  let x : u64;

  if x > 100 {
    return x;
  } else {
    return 100;
  }
}
fn return_bool() -> bool {
  let x : bool;
  if x {
    return x;
  } else {
    return !x;
  }
}
fn return_f32() -> f32 {
  let x = 10.0;
  let y : bool;
  if y {
    return x / 2.0;
  } else {
    return x * 2.0;
  }
}
fn return_f64() -> f64 {
  let x : f64;
  if x <= 10.0 && x >= -10.0 {
    return x;
  } else {
    return 0.0;
  }
}

// Function Calls -- parameters
fn test_22() {
  check_u32(4);
  check_u32(123);
  check_u32(119);
}
fn check_u32(x : u32) {
  if x % 2 == 0 {
    assert!(x < 119)
  } else if x % 3 == 0 {
    assert!(x > 119)
  } else {
    assert!(x == 119)
  }
}*/












// Demo -- "Prove" the Leibniz Method for Approximating Pi
fn test_23() {
  let x : u32;
  let pi = 3.14159265359;

  let x_iters = leibniz_pi(x);
  let x_plus_1_iters = leibniz_pi(x + 1);

  // prove approximation is within 0.1 of pi, given enough iterations
  if x >= 9 {
    let diff = abs_f32(x_iters - pi);
    assert!(diff < 0.1);
  }

  // prove that each iteration improves from the previous
  assert!(abs_f32(x_plus_1_iters - pi) < abs_f32(x_iters - pi));
}

fn leibniz_pi(num_iterations : u32) -> f32 {
  let mut i = 0;
  let mut denominator = 1.0;
  let mut sign = 1.0;
  let mut running_total = 1.0;

  while i < num_iterations  {
    // prepare current step
    denominator += 2.0;
    sign *= -1.0;
    i += 1;
    // add current step
    running_total += sign / denominator;
  }

  // above formula calculates pi / 4;
  running_total *= 4.0;

  return running_total;
}

fn abs_f32(value : f32) -> f32 {
  if value >= 0.0 {
    return value;
  } else {
    return -value;
  }
}











/*

// Unnecessary Unsafe Blocks -- need more implemented features to test something that actually requires unsafe
fn test_24() {
  let x = unsafe {
		assert!(true);
    5
	};

  assert!(x == 5);
}

// Function return-less returns, Function call as parameter to Function Call
fn test_25() {
  assert!(x_plus_two_1(0) == 2);
  assert!(x_plus_two_2(1) == 3);
  assert!(x_plus_two_1(x_plus_two_2(0) + 1) == 5);
}
fn x_plus_two_1(x : u32) -> u32 {
  x + 2
}
fn x_plus_two_2(x : u32) -> u32 {
  let y = x + 1;
  {
    let z = 1;
    y + z
  }
}*/

/*// weird cases
fn main() {
  // Unsolved -- this causes a syntax error on parsing? Is the lexerfile out of date? This line compiles and runs cleanly through cargo.
  //assert!({let x = 5; x} == 5);
}*/
