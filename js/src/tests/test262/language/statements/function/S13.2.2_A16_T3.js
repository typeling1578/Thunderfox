// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: FunctionExpression within a new statement is admitted
es5id: 13.2.2_A16_T3
description: >
    Using "is __obj = new function __func(arg){this.prop=arg; return
    {feat: ++arg}}(5)" as FunctionExpression
---*/

//////////////////////////////////////////////////////////////////////////////
//CHECK#1
if (typeof __func !== "undefined") {
	throw new Test262Error('#1: typeof __func === "undefined"');
}
//
//////////////////////////////////////////////////////////////////////////////

var __obj = new function __func(arg){this.prop=arg; return {feat: ++arg}}(5);

//////////////////////////////////////////////////////////////////////////////
//CHECK#2
if (__obj.prop !== undefined) {
	throw new Test262Error('#2: __obj.prop === undefined. Actual: __obj.prop ==='+__obj.prop);
}
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//CHECK#3
if (__obj.feat !== 6) {
	throw new Test262Error('#3: __obj.feat === 6. Actual: __obj.feat ==='+__obj.feat);
}
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//CHECK#4
if (typeof __func !== "undefined") {
	throw new Test262Error('#4: typeof __func === "undefined". Actual: typeof __func ==='+typeof __func);
}
//
//////////////////////////////////////////////////////////////////////////////

reportCompare(0, 0);
