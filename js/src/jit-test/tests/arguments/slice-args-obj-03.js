function escape() { with ({}) {} }

function foo(i) {
  return i;
}

function bar(n) {
  escape(arguments);
  var args = Array.prototype.slice.call(arguments);
  return foo(args[0]);
}

function baz(a, n) {
  return bar(n);
}

var sum = 0;
for (var i = 0; i < 10000; i++) {
  sum += baz(0, 1);
}
assertEq(sum, 10000);
