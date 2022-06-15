browser.storage.local.get({
    firstStart: true
}, async function(options) {
    if (options.firstStart) {
        await browser.browserLangSettings.setAppLangToSystemLang();
        browser.storage.local.set({
            firstStart: false
        })
        await browser.aboutConfigPrefs.setCharPref("browser.contentblocking.category", "strict");
    }
})
