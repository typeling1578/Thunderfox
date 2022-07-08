{
    const { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm");
    const { OS } = ChromeUtils.import("resource://gre/modules/osfile.jsm");
    const { FileUtils } = ChromeUtils.import("resource://gre/modules/FileUtils.jsm");
    let io_service = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);
    let style_sheet_service = Cc["@mozilla.org/content/style-sheet-service;1"].getService(Ci.nsIStyleSheetService);

    const CSS_THEMES_ENABLED_PREF = "browser.theme.enabledCSSList";
    const REGISTERED_TYPE = style_sheet_service["AGENT_SHEET"];

    const profileDir = Services.dirsvc.get("ProfD", Ci.nsIFile);
    const userChromeCSS_dir = FileUtils.File(OS.Path.join(profileDir.path, "userChromeCSS"));
    let csses = null;
    let loaded_csses = [];

    let load_dir = function(){
        let dirs = [];
        if (!userChromeCSS_dir.exists()) {
            return dirs;
        }
        let dir_entries = userChromeCSS_dir.directoryEntries;
        while (dir_entries.hasMoreElements()) {
            let dir = dir_entries.getNext().QueryInterface(Ci.nsIFile);
            if (dir.exists() && dir.isDirectory()) {
                dirs.push(dir);
            }
        }
        return dirs;
    }

    let css_info_from_dir = function(dirs){
        let csses = [];
        dirs.forEach(dir => {
            let css_path = OS.Path.join(dir.path, "userChrome.css");
            let css = FileUtils.File(css_path);
            if (css.exists() && css.isFile()) {
                csses.push({
                    "css": css,
                    "dirname": dir.displayName
                })
            }
        })
        return csses
    }

    let load_css = function(css){
        let css_path = OS.Path.toFileURI(css.path);
        let uriObj = io_service.newURI(css_path, null, null);
        if (!style_sheet_service.sheetRegistered(uriObj, REGISTERED_TYPE)) {
            style_sheet_service.loadAndRegisterSheet(uriObj, REGISTERED_TYPE);
        }
        loaded_csses.push(css);
    }

    let unload_csses = function(){
        loaded_csses.forEach(css => {
            let css_path = OS.Path.toFileURI(css.path);
            let uriObj = io_service.newURI(css_path, null, null);
            if (style_sheet_service.sheetRegistered(uriObj, REGISTERED_TYPE)) {
                style_sheet_service.unregisterSheet(uriObj, REGISTERED_TYPE);
            }
        })
        loaded_csses = [];
    }

    let pref_load = function(){
        unload_csses();
        let pref = Services.prefs.getStringPref(CSS_THEMES_ENABLED_PREF, "[]");
        let list;
        try {
            list = JSON.parse(pref);
        } catch (e) {
            return;
        }
        list.forEach(dirname => {
            let csses_match = csses.filter(css => css.dirname === dirname);
            if (csses_match && csses_match.length === 1) {
                load_css(csses_match[0].css)
            } else if (csses_match.length > 1){
                console.log("why???????");
            }
        })
    }

    {
        let dirs = load_dir();
        csses = css_info_from_dir(dirs);
        pref_load();
        Services.prefs.addObserver(CSS_THEMES_ENABLED_PREF, function(){
            let dirs = load_dir();
            csses = css_info_from_dir(dirs);
            pref_load();
        })
    }
}
