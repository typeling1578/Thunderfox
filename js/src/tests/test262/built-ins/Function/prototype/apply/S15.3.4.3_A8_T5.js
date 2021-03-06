// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: Function.prototype.apply can`t be used as [[Construct]] caller
es5id: 15.3.4.3_A8_T5
description: Checking if creating "new Function("this.p1=1").apply" fails
---*/

try {
  var FACTORY = Function("this.p1=1").apply;
  var obj = new FACTORY();
  throw new Test262Error('#1: Function.prototype.apply can\'t be used as [[Construct]] caller');
} catch (e) {
  assert(e instanceof TypeError, 'The result of evaluating (e instanceof TypeError) is expected to be true');
}

reportCompare(0, 0);
