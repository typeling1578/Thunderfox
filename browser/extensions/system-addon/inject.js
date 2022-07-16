const remote_url = "https://raw.githubusercontent.com/typeling1578/Remote-Content/main/projects/Thunderfox/compat_enabled.json";

const regist_content = {
    "youtube_script_v1": {
        "js": [
            {"file": "inject/youtube-v1.js"},
        ],
        "matches": [
            "*://www.youtube.com/*",
            "*://music.youtube.com/*",
        ],
        "runAt": "document_start",
    },
};

(async() => {
    let result;
    let result_json;
    let retry_count = 0;
    while (true) {
        try {
            result = await fetch(remote_url);
            if (result.status === 200) {
                result_json = await result.json();
            } else {
                throw result.status + ": " + result.statusText;
            }
            if (!Array.isArray(result_json)) {
                throw "Not an array";
            }
            break;
        } catch (e) {
            console.log(e);
            if (retry_count < 2) {
                retry_count++;
                await new Promise(resolve => setTimeout(() => resolve(), 1000))
            } else {
                return;
            }
        }
    }

    result_json.forEach(id => {
        if (typeof regist_content[id] !== "undefined") {
            browser.contentScripts.register(regist_content[id]);
        }
    })
})();
