// |reftest| async
// Copyright 2014 Cubane Canada, Inc.  All rights reserved.
// See LICENSE for details.

/*---
info: |
    PerformPromiseThen
    Ref 25.4.5.3.1
es6id: S25.4.5.3_A4.1_T1
author: Sam Mikes
description: Promise.prototype.then accepts 'undefined' as arg1, arg2
flags: [async]
---*/

var arg = {};
var p = Promise.resolve(arg);

p.then(undefined, undefined)
  .then(function(result) {
  assert.sameValue(result, arg, 'The value of result is expected to equal the value of arg');
}).then($DONE, $DONE);
