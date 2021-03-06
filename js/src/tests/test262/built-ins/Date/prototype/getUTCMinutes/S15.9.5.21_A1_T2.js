// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: The Date.prototype property "getUTCMinutes" has { DontEnum } attributes
esid: sec-date.prototype.getutcminutes
description: Checking absence of DontDelete attribute
---*/
assert.notSameValue(
  delete Date.prototype.getUTCMinutes,
  false,
  'The value of delete Date.prototype.getUTCMinutes is not false'
);

assert(
  !Date.prototype.hasOwnProperty('getUTCMinutes'),
  'The value of !Date.prototype.hasOwnProperty(\'getUTCMinutes\') is expected to be true'
);

// TODO: Convert to verifyProperty() format.

reportCompare(0, 0);
