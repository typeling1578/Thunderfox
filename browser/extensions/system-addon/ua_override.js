const targetPages = [
  "*://typeling1578.github.io/*",
];

function rewriteUserAgentHeaderAsync(e) {
  let asyncRewrite = new Promise(async(resolve, reject) => {
    for (let header of e.requestHeaders) {
      if (header.name.toLowerCase() === "user-agent") {
        const AppVersion = await browser.browserInfo.getDisplayVersion();
        header.value += " Thunderfox/" + AppVersion;
      }
    }
    resolve({requestHeaders: e.requestHeaders});
  });

  return asyncRewrite;
}

browser.webRequest.onBeforeSendHeaders.addListener(
  rewriteUserAgentHeaderAsync,
  {urls: targetPages},
  ["blocking", "requestHeaders"]
);
