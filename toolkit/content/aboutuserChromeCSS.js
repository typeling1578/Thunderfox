const { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm");
const { OS } = ChromeUtils.import("resource://gre/modules/osfile.jsm");
const { FileUtils } = ChromeUtils.import("resource://gre/modules/FileUtils.jsm");

const CSS_THEMES_ENABLED_PREF = "browser.theme.enabledCSSList";

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

let theme_enable = function(dirname) {
    let pref = Services.prefs.getStringPref(CSS_THEMES_ENABLED_PREF, "[]");
    let list
    try {
        list = JSON.parse(pref);
    } catch (e) {
        Services.prefs.setStringPref(CSS_THEMES_ENABLED_PREF, "[]");
        pref = Services.prefs.getStringPref(CSS_THEMES_ENABLED_PREF, "[]");
        list = JSON.parse(pref);
    }
    if (!list.includes(dirname)) {
        let csses_match = csses.filter(css => css.dirname === dirname);
        if (csses_match && csses_match.length === 1) {
            list.push(csses_match[0].dirname);
        } else if (csses_match.length > 1){
            console.log("why???????");
        }
        Services.prefs.setStringPref(CSS_THEMES_ENABLED_PREF, JSON.stringify(list));
    }
}

let theme_disable = function(dirname) {
    let pref = Services.prefs.getStringPref(CSS_THEMES_ENABLED_PREF, "[]");
    let list
    try {
        list = JSON.parse(pref);
    } catch (e) {
        Services.prefs.setStringPref(CSS_THEMES_ENABLED_PREF, "[]");
        pref = Services.prefs.getStringPref(CSS_THEMES_ENABLED_PREF, "[]");
        list = JSON.parse(pref);
    }
    if (list.includes(dirname)) {
        list = list.filter(p_dirname => p_dirname !== dirname);
        Services.prefs.setStringPref(CSS_THEMES_ENABLED_PREF, JSON.stringify(list));
    }
}

let elemload = function(obj, enabled){
    let elem = document.querySelector("#userchromecss-template").children[0].cloneNode(true);
    elem.querySelector(".title").innerText = obj.dirname;
    elem.querySelector("input").checked = enabled;
    elem.querySelector("input").addEventListener("change", function(e){
        if (e.target.checked) {
            theme_enable(obj.dirname);
        } else {
            theme_disable(obj.dirname);
        }
    })
    document.querySelector("#userchromecsses").appendChild(elem);
}

let load = function(){
    document.querySelector("#userchromecsses").innerHTML = "";
    let pref = Services.prefs.getStringPref(CSS_THEMES_ENABLED_PREF, "[]");
    let list;
    try {
        list = JSON.parse(pref);
    } catch (e) {
        list = [];
    }
    csses.forEach(css => {
        elemload(css, list.includes(css.dirname));
    })
}

document.addEventListener("DOMContentLoaded", function(){
    {
        let dirs = load_dir();
        csses = css_info_from_dir(dirs);
        load();
        Services.prefs.addObserver(CSS_THEMES_ENABLED_PREF, function(){
            let dirs = load_dir();
            csses = css_info_from_dir(dirs);
            load();
        })
    }
})