// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: |
    When String is called as a function rather than as a constructor, it
    performs a type conversion
es5id: 15.5.1.1_A1_T12
description: Call String(1/"a"), String("b"* null) and String(Number.NaN)
---*/

var __str = String(1 / "a");

//////////////////////////////////////////////////////////////////////////////
//CHECK#1
if (typeof __str !== "string") {
  throw new Test262Error('#1: __str = String(1/"a"); typeof __str === "string". Actual: typeof __str ===' + typeof __str);
}
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//CHECK#2
if (__str !== "NaN") {
  throw new Test262Error('#2: __str = String(1/"a"); __str === "NaN". Actual: __str ===' + __str);
}
//
//////////////////////////////////////////////////////////////////////////////

__str = String("b" * null);

//////////////////////////////////////////////////////////////////////////////
//CHECK#3
if (typeof __str !== "string") {
  throw new Test262Error('#3: __str = String("b"*null); typeof __str === "string". Actual: typeof __str ===' + typeof __str);
}
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//CHECK#4
if (__str !== "NaN") {
  throw new Test262Error('#4: __str = String("b"*null); __str === "NaN". Actual: __str ===' + __str);
}
//
//////////////////////////////////////////////////////////////////////////////

__str = String(Number.NaN);

//////////////////////////////////////////////////////////////////////////////
//CHECK#5
if (typeof __str !== "string") {
  throw new Test262Error('#5: __str = String(Number.NaN); typeof __str === "string". Actual: typeof __str ===' + typeof __str);
}
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//CHECK#6
if (__str !== "NaN") {
  throw new Test262Error('#6: __str = String(Number.NaN); __str === "NaN". Actual: __str ===' + __str);
}
//
//////////////////////////////////////////////////////////////////////////////

reportCompare(0, 0);
