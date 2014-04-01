/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

function test()
{

  function makeStylesheet(selector) {
    return ("data:text/css;charset=UTF-8," +
            encodeURIComponent(selector + " { }"));
  }

  function makeDocument(stylesheets, framedDocuments) {
    stylesheets = stylesheets || [];
    framedDocuments = framedDocuments || [];
    return "data:text/html;charset=UTF-8," + encodeURIComponent(
      Array.prototype.concat.call(
        ["<!DOCTYPE html>",
         "<html>",
         "<head>",
         "<title>Bug 740541</title>"],
        stylesheets.map(function (sheet) {
          return '<link rel="stylesheet" type="text/css" href="'+sheet+'">';
        }),
        ["</head>",
         "<body>"],
        framedDocuments.map(function (doc) {
          return '<iframe src="'+doc+'"></iframe>';
        }),
        ["</body>",
         "</html>"]
      ).join("\n"));
  }

  const DOCUMENT_WITH_INLINE_STYLE = "data:text/html;charset=UTF-8," +
          encodeURIComponent(
            ["<!DOCTYPE html>",
             "<html>",
             " <head>",
             "  <title>Bug 740541</title>",
             '  <style type="text/css">',
             "    .something {",
             "    }",
             "  </style>",
             " </head>",
             " <body>",
             " </body>",
             " </html>"
            ].join("\n"));

  const FOUR = TEST_BASE_HTTP + "four.html";

  const SIMPLE = TEST_BASE_HTTP + "simple.css";

  const SIMPLE_DOCUMENT = TEST_BASE_HTTP + "simple.html";


  const TESTCASE_URI = makeDocument(
    [makeStylesheet(".a")],
    [makeDocument([],
                  [FOUR,
                   DOCUMENT_WITH_INLINE_STYLE]),
     makeDocument([makeStylesheet(".b"),
                   SIMPLE],
                  [makeDocument([makeStylesheet(".c")],
                                [])]),
     makeDocument([SIMPLE], []),
     SIMPLE_DOCUMENT
    ]);

  const EXPECTED_STYLE_SHEET_COUNT = 12;

  waitForExplicitFinish();

  // Wait for events until the right number of editors has been opened.
  addTabAndOpenStyleEditors(EXPECTED_STYLE_SHEET_COUNT, () => finish());

  content.location = TESTCASE_URI;
}
