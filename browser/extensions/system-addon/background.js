browser.storage.local.get({
    firstStart: true
}, async function(options) {
    if (options.firstStart) {
        await browser.aboutConfigPrefs.setCharPref("browser.contentblocking.category", "strict");
        browser.storage.local.set({
            firstStart: false
        })
    }
})
