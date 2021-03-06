/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

module.exports = {
  rules: {
    "mozilla/var-only-at-top-level": "error",
    "require-jsdoc": [
      "error",
      {
        require: {
          FunctionDeclaration: true,
          MethodDefinition: true,
          ClassDeclaration: true,
          ArrowFunctionExpression: false,
          FunctionExpression: false,
        },
      },
    ],
    "valid-jsdoc": [
      "error",
      {
        prefer: {
          return: "returns",
        },
        preferType: {
          Boolean: "boolean",
          Number: "number",
          String: "string",
          Object: "object",
          bool: "boolean",
        },
        requireParamDescription: true,
        requireReturn: false,
        requireReturnDescription: false,
      },
    ],
    "no-unused-expressions": "error",
  },
};
