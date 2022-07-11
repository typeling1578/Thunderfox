const UA_PRESET = {
    "chrome": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/103.0.0.0 Safari/537.36"
}

const override_sites = [
    {
        "domain": "plus.nhk.jp",//日本の恥
        "ua": UA_PRESET["chrome"]
    },
    {
        "domain": "recochoku.jp",
        "ua": UA_PRESET["chrome"]
    },
    {
        "domain": "www.mobile.pasmo.jp",
        "ua": UA_PRESET["chrome"]
    },
    {
        "domain": "business.apple.com",
        "ua": UA_PRESET["chrome"]
    }
]

function rewriteUserAgentHeader(e) {
    let refurl = e.originUrl;

    if ((refurl === null || refurl === undefined) && !(e.type == "main_frame" || e.type == "sub_frame" || e.type == "object")) {
        return;
    }

    let URL_obj = "";
    if (e.type == "main_frame" || e.type == "sub_frame" || e.type == "object") {
        URL_obj = new URL(e.url);
    } else {
        URL_obj = new URL(refurl);
    }

    for (override_site of override_sites) {
        if (URL_obj.hostname === override_site["domain"]) {
            e.requestHeaders.forEach(function(header){
                if (header.name.toLowerCase() === "user-agent") {
                    header.value = override_site["ua"]
                }
            })
            return { requestHeaders: e.requestHeaders }
        }
    }
    return;
}

browser.webRequest.onBeforeSendHeaders.addListener(
    rewriteUserAgentHeader,
    { urls: ["<all_urls>"] },
    ["blocking", "requestHeaders"]
);
