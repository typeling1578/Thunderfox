browser.storage.local.get({
    firstStart: true
}, async function(options) {
    if (options.firstStart) {
        await browser.browserLangSettings.setAppLangToSystemLang();
        browser.storage.local.set({
            firstStart: false
        })
    }
})
