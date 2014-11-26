/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Test Yahoo search plugin URLs
 */

"use strict";

const BROWSER_SEARCH_PREF = "browser.search.";

function test() {
  let engine = Services.search.getEngineByName("Yahoo");
  ok(engine, "Yahoo");

  let base = "https://search.yahoo.com/yhs/search?p=foo&ei=UTF-8&hspart=mozilla&hsimp=yhs-001";
  let url;

  // Test search URLs (including purposes).
  url = engine.getSubmission("foo").uri.spec;
  is(url, base, "Check search URL for 'foo'");

  // Check search suggestion URL.
  url = engine.getSubmission("foo", "application/x-suggestions+json").uri.spec;
  is(url, "https://search.yahoo.com/sugg/ff?output=fxjson&appid=ffd&command=foo", "Check search suggestion URL for 'foo'");

  // Check all other engine properties.
  const EXPECTED_ENGINE = {
    name: "Yahoo",
    alias: null,
    description: "Yahoo Search",
    searchForm: "https://search.yahoo.com/",
    type: Ci.nsISearchEngine.TYPE_MOZSEARCH,
    hidden: false,
    wrappedJSObject: {
      queryCharset: "UTF-8",
      "_iconURL": "data:image/x-icon;base64,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",
      _urls : [
        {
          type: "application/x-suggestions+json",
          method: "GET",
          template: "https://search.yahoo.com/sugg/ff",
          params: [
            {
              name: "output",
              value: "fxjson",
              purpose: undefined,
            },
            {
              name: "appid",
              value: "ffd",
              purpose: undefined,
            },
            {
              name: "command",
              value: "{searchTerms}",
              purpose: undefined,
            },
          ],
        },
        {
          type: "text/html",
          method: "GET",
          template: "https://search.yahoo.com/yhs/search",
          params: [
            {
              name: "p",
              value: "{searchTerms}",
              purpose: undefined,
            },
            {
              name: "ei",
              value: "UTF-8",
              purpose: undefined,
            },
            {
              name: "hspart",
              value: "mozilla",
              purpose: undefined,
            },
            {
              name: "hsimp",
              value: "yhs-001",
              purpose: undefined,
            },
          ],
          mozparams: {},
        },
      ],
    },
  };

  isSubObjectOf(EXPECTED_ENGINE, engine, "Yahoo");
}
