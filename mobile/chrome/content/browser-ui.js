// -*- Mode: js2; tab-width: 2; indent-tabs-mode: nil; js2-basic-offset: 2; js2-skip-preprocessor-directives: t; -*-
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Mobile Browser.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mark Finkle <mfinkle@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyGetter(this, "PluralForm", function() {
  Cu.import("resource://gre/modules/PluralForm.jsm");
  return PluralForm;
});

XPCOMUtils.defineLazyGetter(this, "PlacesUtils", function() {
  Cu.import("resource://gre/modules/utils.js");
  return PlacesUtils;
});

const TOOLBARSTATE_LOADING  = 1;
const TOOLBARSTATE_LOADED   = 2;

[
  [
    "gHistSvc",
    "@mozilla.org/browser/nav-history-service;1",
    [Ci.nsINavHistoryService, Ci.nsIBrowserHistory]
  ],
  [
    "gFaviconService",
     "@mozilla.org/browser/favicon-service;1",
     [Ci.nsIFaviconService]
  ],
  [
    "gIOService",
    "@mozilla.org/network/io-service;1",
    [Ci.nsIIOService],
  ],
  [
    "gURIFixup",
    "@mozilla.org/docshell/urifixup;1",
    [Ci.nsIURIFixup]
  ],
  [
    "gPrefService",
    "@mozilla.org/preferences-service;1",
    [Ci.nsIPrefBranch2]
  ],
  [
    "gFocusManager",
    "@mozilla.org/focus-manager;1",
    [Ci.nsIFocusManager]
  ],
  [
    "gObserverService",
    "@mozilla.org/observer-service;1",
    [Ci.nsIObserverService]
  ]
].forEach(function (service) {
  let [name, contract, ifaces] = service;
  window.__defineGetter__(name, function () {
    delete window[name];
    window[name] = Cc[contract].getService(ifaces.splice(0, 1)[0]);
    if (ifaces.length)
      ifaces.forEach(function (i) { return window[name].QueryInterface(i); });
    return window[name];
  });
});

var BrowserUI = {
  _edit : null,
  _throbber : null,
  _favicon : null,
  _dialogs: [],

  _domWillOpenModalDialog: function(e) {
    if (!e.isTrusted)
      return;

    // We're about to open a modal dialog, make sure the opening
    // tab is brought to the front.

    let window = e.target.top;
    for (let i = 0; i < Browser._tabs.length; i++) {
      if (Browser._tabs[i].browser.contentWindow == window) {
        Browser.selectedTab = Browser._tabs[i];
        break;
      }
    }
  },

  _titleChanged : function(aDocument) {
    var browser = Browser.selectedBrowser;
    if (browser && aDocument != browser.contentDocument)
      return;

    var caption = aDocument.title;
    if (!caption) {
      caption = this.getDisplayURI(browser);
      if (caption == "about:blank")
        caption = "";
    }

    this._setURI(caption);
  },

  /*
   * Dispatched by window.close() to allow us to turn window closes into tabs
   * closes.
   */
  _domWindowClose: function (aEvent) {
    if (!aEvent.isTrusted)
      return;

    // Find the relevant tab, and close it.
    let browsers = Browser.browsers;
    for (let i = 0; i < browsers.length; i++) {
      if (browsers[i].contentWindow == aEvent.target) {
        Browser.closeTab(Browser.getTabAtIndex(i));
        aEvent.preventDefault();
        break;
      }
    }
  },

  _linkAdded : function(aEvent) {
    let link = aEvent.originalTarget;
    if (!link || !link.href)
      return;

    if (/\bicon\b/i(link.rel)) {
      // Must have an owner document and not be in a frame
      var ownerDoc = link.ownerDocument;
      if (!ownerDoc || ownerDoc.defaultView.frameElement)
        return;

      let tab = Browser.getTabForDocument(ownerDoc);
      tab.setIcon(link.href);
      // If the link changes after pageloading, update it right away.
      // otherwise we wait until the pageload finishes
      if ((tab.browser == Browser.selectedBrowser) && !tab.isLoading())
        this._updateIcon(tab.browser.mIconURL);
    }
    else if (/\bsearch\b/i(link.rel)) {
      var type = link.type && link.type.toLowerCase();
      type = type.replace(/^\s+|\s*(?:;.*)?$/g, "");
      if (type == "application/opensearchdescription+xml" && link.title && /^(?:https?|ftp):/i.test(link.href)) {
        var engine = { title: link.title, href: link.href };

        BrowserSearch.addPageSearchEngine(engine, link.ownerDocument);
      }
    }
  },

  _updateButtons : function(aBrowser) {
    let back = document.getElementById("cmd_back");
    let forward = document.getElementById("cmd_forward");

    back.setAttribute("disabled", !aBrowser.canGoBack);
    forward.setAttribute("disabled", !aBrowser.canGoForward);
  },

  _updateToolbarButton: function() {
    let icons = document.getElementById("urlbar-icons");
    if (Browser.selectedTab.isLoading() && icons.getAttribute("mode") != "loading") {
      icons.setAttribute("mode", "loading");
    }
    else if (icons.getAttribute("mode") != "view") {
      icons.setAttribute("mode", "view");
    }
  },

  _tabSelect : function(aEvent) {
    let browser = Browser.selectedBrowser;
    this._titleChanged(browser.contentDocument);
    this._updateToolbarButton();
    this._updateButtons(browser);
    this._updateIcon(browser.mIconURL);
    this.updateStar();
  },

  showToolbar : function showToolbar(aEdit) {
    this.hidePanel();
    this._editURI(aEdit);
  },

  _toolbarLocked: 0,

  isToolbarLocked: function isToolbarLocked() {
    return this._toolbarLocked;
  },

  lockToolbar: function lockToolbar() {
    this._toolbarLocked++;
    document.getElementById("toolbar-moveable-container").top = "0";
    if (this._toolbarLocked == 1)
      Browser.forceChromeReflow();
  },

  unlockToolbar: function unlockToolbar() {
    if (!this._toolbarLocked)
      return;

    this._toolbarLocked--;
    if (!this._toolbarLocked)
      document.getElementById("toolbar-moveable-container").top = "";
  },

  _setURI: function _setURI(aCaption) {
    if (this.isAutoCompleteOpen())
      this._edit.defaultValue = aCaption;
    else
      this._edit.value = aCaption;
  },

  _editURI : function _editURI(aEdit) {
    var icons = document.getElementById("urlbar-icons");
    if (aEdit && icons.getAttribute("mode") != "edit") {
      icons.setAttribute("mode", "edit");
      this._edit.defaultValue = this._edit.value;

      let urlString = this.getDisplayURI(Browser.selectedBrowser);
      if (urlString == "about:blank")
        urlString = "";
      this._edit.value = urlString;

      // This is a workaround for bug 488420, needed to cycle focus for the
      // IME state to be set properly. Testing shows we only really need to
      // do this the first time.
      this._edit.blur();
      gFocusManager.setFocus(this._edit, Ci.nsIFocusManager.FLAG_NOSCROLL);
    }
    else if (!aEdit) {
      this._updateToolbarButton();
    }
  },

  _closeOrQuit: function _closeOrQuit() {
    // Close active dialog, if we have one. If not then close the application.
    let dialog = this.activeDialog;
    if (dialog)
      dialog.close();
    else
      window.close();
  },

  get activeDialog() {
    // Return the topmost dialog
    if (this._dialogs.length)
      return this._dialogs[this._dialogs.length - 1];
    return null;
  },

  pushDialog : function pushDialog(aDialog) {
    // If we have a dialog push it on the stack and set the attr for CSS
    if (aDialog) {
      this.lockToolbar();
      this._dialogs.push(aDialog);
      document.getElementById("toolbar-main").setAttribute("dialog", "true");
      Elements.contentShowing.setAttribute("disabled", "true");
    }
  },

  popDialog : function popDialog() {
    if (this._dialogs.length) {
      this._dialogs.pop();
      this.unlockToolbar();
    }

    // If no more dialogs are being displayed, remove the attr for CSS
    if (!this._dialogs.length) {
      document.getElementById("toolbar-main").removeAttribute("dialog");
      Elements.contentShowing.removeAttribute("disabled");
    }
  },

  pushPopup: function pushPopup(aPanel, aElements) {
    this._hidePopup();
    this._popup =  { "panel": aPanel,
                     "elements": (aElements instanceof Array) ? aElements : [aElements] };
    this._dispatchPopupChanged();
  },

  popPopup: function popPopup() {
    this._popup = null;
    this._dispatchPopupChanged();
  },

  _dispatchPopupChanged: function _dispatchPopupChanged() {
    let stack = document.getElementById("stack");
    let event = document.createEvent("Events");
    event.initEvent("PopupChanged", true, false);
    event.popup = this._popup;
    stack.dispatchEvent(event);
  },

  _hidePopup: function _hidePopup() {
    if (!this._popup)
      return;
    let panel = this._popup.panel;
    if (panel.hide)
      panel.hide();
  },

  _isEventInsidePopup: function _isEventInsidePopup(aEvent) {
    if (!this._popup)
      return false;
    let elements = this._popup.elements;
    let targetNode = aEvent ? aEvent.target : null;
    while (targetNode && elements.indexOf(targetNode) == -1)
      targetNode = targetNode.parentNode;
    return targetNode ? true : false;
  },

  switchPane : function switchPane(id) {
    let button = document.getElementsByAttribute("linkedpanel", id)[0];
    if (button)
      button.checked = true;

    let pane = document.getElementById(id);
    document.getElementById("panel-items").selectedPanel = pane;
  },

  get toolbarH() {
    if (!this._toolbarH) {
      let toolbar = document.getElementById("toolbar-main");
      this._toolbarH = toolbar.boxObject.height;
    }
    return this._toolbarH;
  },

  get sidebarW() {
    if (!this._sidebarW) {
      let sidebar = document.getElementById("browser-controls");
      this._sidebarW = sidebar.boxObject.width;
    }
    return this._sidebarW;
  },

  get starButton() {
    delete this.starButton;
    return this.starButton = document.getElementById("tool-star");
  },

  sizeControls : function(windowW, windowH) {
    // tabs
    document.getElementById("tabs").resize();

    // Site menu
    PageActions.resize();

    // awesomebar
    let popup = document.getElementById("popup_autocomplete");
    popup.top = this.toolbarH;
    popup.height = windowH - this.toolbarH;
    popup.width = windowW;

    // form helper
    let formHelper = document.getElementById("form-helper-container");
    formHelper.top = windowH - formHelper.getBoundingClientRect().height;
  },

  init : function() {
    this._edit = document.getElementById("urlbar-edit");
    this._throbber = document.getElementById("urlbar-throbber");
    this._favicon = document.getElementById("urlbar-favicon");
    this._favicon.addEventListener("error", this, false);

    let urlbarEditArea = document.getElementById("urlbar-editarea");
    urlbarEditArea.addEventListener("click", this, false);
    urlbarEditArea.addEventListener("mousedown", this, false);

    document.getElementById("toolbar-main").ignoreDrag = true;

    let tabs = document.getElementById("tabs");
    tabs.addEventListener("TabSelect", this, true);
    tabs.addEventListener("TabOpen", this, true);

    let browsers = document.getElementById("browsers");
    browsers.addEventListener("DOMWindowClose", this, true);

    // XXX these really want to listen to only the current browser
    browsers.addEventListener("DOMTitleChanged", this, true);
    browsers.addEventListener("DOMLinkAdded", this, true);
    browsers.addEventListener("DOMWillOpenModalDialog", this, true);

    // listening mousedown for automatically dismiss some popups (e.g. larry)
    window.addEventListener("mousedown", this, true);

    // listening escape to dismiss dialog on VK_ESCAPE
    window.addEventListener("keypress", this, true);

    // Push the panel initialization out of the startup path
    // (Using an event because we have no good way to delay-init [Bug 535366])
    browsers.addEventListener("load", function() {
      // We only want to delay one time
      browsers.removeEventListener("load", arguments.callee, true);
      
      // We unhide the panelUI so the XBL and settings can initialize
      Elements.panelUI.hidden = false;

      // Init the views
      ExtensionsView.init();
      DownloadsView.init();
      PreferencesView.init();
      ConsoleView.init();
    }, true);
  },

  uninit : function() {
    ExtensionsView.uninit();
    ConsoleView.uninit();
  },

  update : function(aState) {
    let icons = document.getElementById("urlbar-icons");
    let browser = Browser.selectedBrowser;

    switch (aState) {
      case TOOLBARSTATE_LOADED:
        if (icons.getAttribute("mode") != "edit")
          icons.setAttribute("mode", "view");

        this._updateIcon(browser.mIconURL);
        break;

      case TOOLBARSTATE_LOADING:
        if (icons.getAttribute("mode") != "edit")
          icons.setAttribute("mode", "loading");

        browser.mIconURL = "";
        this._updateIcon();
        break;
    }
  },

  _updateIcon : function(aIconSrc) {
    this._favicon.src = aIconSrc || "";
    if (Browser.selectedTab.isLoading()) {
      this._throbber.hidden = false;
      this._throbber.setAttribute("loading", "true");
      this._favicon.hidden = true;
    }
    else {
      this._favicon.hidden = false;
      this._throbber.hidden = true;
      this._throbber.removeAttribute("loading");
    }
  },

  getDisplayURI : function(browser) {
    var uri = browser.currentURI;

    if (!this._URIFixup)
      this._URIFixup = Cc["@mozilla.org/docshell/urifixup;1"].getService(Ci.nsIURIFixup);

    try {
      uri = this._URIFixup.createExposableURI(uri);
    } catch (ex) {}

    return uri.spec;
  },

  /* Set the location to the current content */
  updateURI : function() {
    var browser = Browser.selectedBrowser;

    // FIXME: deckbrowser should not fire TabSelect on the initial tab (bug 454028)
    if (!browser.currentURI)
      return;

    // Update the navigation buttons
    this._updateButtons(browser);

    // Close the forms assistant
    FormHelper.close();

    // Check for a bookmarked page
    this.updateStar();

    var urlString = this.getDisplayURI(browser);
    if (urlString == "about:blank")
      urlString = "";

    this._setURI(urlString);
  },

  goToURI : function(aURI) {
    aURI = aURI || this._edit.value;
    if (!aURI)
      return;

    // Make sure we're online before attempting to load
    Util.forceOnline();

    // Give the new page lots of room
    Browser.hideSidebars();
    this.closeAutoComplete(true);

    this._edit.value = aURI;

    var flags = Ci.nsIWebNavigation.LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP;
    getBrowser().loadURIWithFlags(aURI, flags, null, null);

    gHistSvc.markPageAsTyped(gURIFixup.createFixupURI(aURI, 0));
  },

  showAutoComplete : function showAutoComplete() {
    if (this.isAutoCompleteOpen())
      return;

    BrowserSearch.updateSearchButtons();

    this._edit.showHistoryPopup();
  },

  closeAutoComplete: function closeAutoComplete(aResetInput) {
    if (!this.isAutoCompleteOpen())
      return;

    if (aResetInput)
      this._edit.popup.close();
    else
      this._edit.popup.closePopup();
  },

  isAutoCompleteOpen: function isAutoCompleteOpen() {
    return this._edit.popup.popupOpen;
  },

  doButtonSearch : function(button) {
    if (!("engine" in button) || !button.engine)
      return;

    // We don't want the button to look pressed for now
    button.parentNode.selectedItem = null;

    // Give the new page lots of room
    Browser.hideSidebars();
    this.closeAutoComplete(false);

    // Make sure we're online before attempting to load
    Util.forceOnline();

    let submission = button.engine.getSubmission(this._edit.value, null);
    let flags = Ci.nsIWebNavigation.LOAD_FLAGS_NONE;
    getBrowser().loadURIWithFlags(submission.uri.spec, flags, null, null, submission.postData);
  },

  updateStar : function() {
    if (PlacesUtils.getMostRecentBookmarkForURI(Browser.selectedBrowser.currentURI) != -1)
      this.starButton.setAttribute("starred", "true");
    else
      this.starButton.removeAttribute("starred");
  },

  newTab : function newTab(aURI) {
    aURI = aURI || "about:blank";
    let tab = Browser.addTab(aURI, true);

    this.hidePanel();

    if (aURI == "about:blank") {
      // Display awesomebar UI
      this.showToolbar(true);
      this.showAutoComplete();
    }
    else {
      // Give the new page lots of room
      Browser.hideSidebars();
      this.closeAutoComplete(true);
    }

    return tab;
  },

  closeTab : function closeTab(aTab) {
    // If no tab is passed in, assume the current tab
    Browser.closeTab(aTab || Browser.selectedTab);
  },

  selectTab : function selectTab(aTab) {
    Browser.selectedTab = aTab;
  },

  isTabsVisible: function isTabsVisible() {
    // The _1, _2 and _3 are to make the js2 emacs mode happy
    let [leftvis,_1,_2,_3] = Browser.computeSidebarVisibility();
    return (leftvis > 0.002);
  },

  showPanel: function showPanel(aPage) {
    Elements.panelUI.left = 0;
    Elements.panelUI.hidden = false;
    Elements.contentShowing.setAttribute("disabled", "true");

    if (aPage != undefined)
      this.switchPane(aPage);
  },

  hidePanel: function hidePanel() {
    Elements.panelUI.hidden = true;
    Elements.contentShowing.removeAttribute("disabled");
  },

  isPanelVisible: function isPanelVisible() {
    return (!Elements.panelUI.hidden && Elements.panelUI.left == 0);
  },

  switchTask: function switchTask() {
    try {
      let phone = Cc["@mozilla.org/phone/support;1"].createInstance(Ci.nsIPhoneSupport);
      phone.switchTask();
    } catch(e) { }
  },

#ifdef WINCE
  updateDefaultBrowser: function updateDefaultBrowser(aSet) {
    try {
      let phone = Cc["@mozilla.org/phone/support;1"].getService(Ci.nsIPhoneSupport);
      if (aSet)
        phone.setDefaultBrowser();
      else
        phone.restoreDefaultBrowser();
    } catch(e) { }
  },
#endif

  handleEvent: function (aEvent) {
    switch (aEvent.type) {
      // Browser events
      case "DOMWillOpenModalDialog":
        this._domWillOpenModalDialog(aEvent);
        break;
      case "DOMTitleChanged":
        this._titleChanged(aEvent.target);
        break;
      case "DOMLinkAdded":
        this._linkAdded(aEvent);
        break;
      case "DOMWindowClose":
        this._domWindowClose(aEvent);
        break;
      case "TabSelect":
        this._tabSelect(aEvent);
        break;
      case "TabOpen":
      {
        if (!this.isTabsVisible() && Browser.selectedTab.chromeTab != aEvent.target)
          NewTabPopup.show(aEvent.target);

          // Workaround to hide the tabstrip if it is partially visible
          // See bug 524469
          let [tabstripV,,,] = Browser.computeSidebarVisibility();
          if (tabstripV > 0 && tabstripV < 1)
            Browser.hideSidebars();

        break;
      }
      // URL textbox events
      case "click":
        this.doCommand("cmd_openLocation");
        break;
      case "keypress":
        if (aEvent.keyCode == aEvent.DOM_VK_ESCAPE) {
          let dialog = this.activeDialog;
          if (dialog)
            dialog.close();
        }
        break;
      case "mousedown":
        if (!this._isEventInsidePopup(aEvent))
          this._hidePopup();

        if (aEvent.detail == 2 &&
            aEvent.button == 0 &&
            gPrefService.getBoolPref("browser.urlbar.doubleClickSelectsAll")) {
          this._edit.editor.selectAll();
          aEvent.preventDefault();
        }
        break;
      // Favicon events
      case "error":
        this._favicon.src = "";
        break;
    }
  },

  supportsCommand : function(cmd) {
    var isSupported = false;
    switch (cmd) {
      case "cmd_back":
      case "cmd_forward":
      case "cmd_reload":
      case "cmd_forceReload":
      case "cmd_stop":
      case "cmd_go":
      case "cmd_openLocation":
      case "cmd_star":
      case "cmd_bookmarks":
      case "cmd_quit":
      case "cmd_close":
      case "cmd_menu":
      case "cmd_newTab":
      case "cmd_closeTab":
      case "cmd_actions":
      case "cmd_panel":
      case "cmd_sanitize":
      case "cmd_zoomin":
      case "cmd_zoomout":
        isSupported = true;
        break;
      default:
        isSupported = false;
        break;
    }
    return isSupported;
  },

  isCommandEnabled : function(cmd) {
    return true;
  },

  doCommand : function(cmd) {
    var browser = getBrowser();
    switch (cmd) {
      case "cmd_back":
        browser.goBack();
        break;
      case "cmd_forward":
        browser.goForward();
        break;
      case "cmd_reload":
        browser.reload();
        break;
      case "cmd_forceReload":
      {
        const reloadFlags = Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_PROXY |
                            Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CACHE;
        browser.reloadWithFlags(reloadFlags);
        break;
      }
      case "cmd_stop":
        browser.stop();
        break;
      case "cmd_go":
        this.goToURI();
        break;
      case "cmd_openLocation":
        this.showToolbar(true);
        this.showAutoComplete();
        break;
      case "cmd_star":
      {
        var bookmarkURI = browser.currentURI;
        var bookmarkTitle = browser.contentDocument.title || bookmarkURI.spec;

        let autoClose = false;

        if (PlacesUtils.getMostRecentBookmarkForURI(bookmarkURI) == -1) {
          let bmsvc = PlacesUtils.bookmarks;
          let bookmarkId = bmsvc.insertBookmark(BookmarkList.mobileRoot, bookmarkURI,
                                                bmsvc.DEFAULT_INDEX,
                                                bookmarkTitle);
          this.updateStar();

          // autoclose the bookmark popup
          autoClose = true;
        }

        // Show/hide bookmark popup
        BookmarkPopup.toggle(autoClose);
        break;
      }
      case "cmd_bookmarks":
        BookmarkList.show();
        break;
      case "cmd_quit":
        goQuitApplication();
        break;
      case "cmd_close":
        this._closeOrQuit();
        break;
      case "cmd_menu":
        break;
      case "cmd_newTab":
        this.newTab();
        break;
      case "cmd_closeTab":
        this.closeTab();
        break;
      case "cmd_sanitize":
      {
        // disable the button temporarily to indicate something happened
        let button = document.getElementById("prefs-clear-data");
        button.disabled = true;
        setTimeout(function() { button.disabled = false; }, 5000);

        Sanitizer.sanitize();
        break;
      }
      case "cmd_panel":
      {
        if (BrowserUI.isPanelVisible())
          this.hidePanel();
        else
          this.showPanel();
        break;
      }
      case "cmd_zoomin":
        Browser.zoom(-1);
        break;
      case "cmd_zoomout":
        Browser.zoom(1);
        break;
    }
  }
};

var PageActions = {
  get _permissionManager() {
    delete this._permissionManager;
    return this._permissionManager = Cc["@mozilla.org/permissionmanager;1"].getService(Ci.nsIPermissionManager);
  },

  get _loginManager() {
    delete this._loginManager;
    return this._loginManager = Cc["@mozilla.org/login-manager;1"].getService(Ci.nsILoginManager);
  },

  // This is easy for an addon to add his own perm type here
  _permissions: ["popup", "offline-app", "geo"],

  _forEachPermissions: function _forEachPermissions(aHost, aCallback) {
    let pm = this._permissionManager;
    for (let i = 0; i < this._permissions.length; i++) {
      let type = this._permissions[i];
      if (!pm.testPermission(aHost, type))
        continue;

      let perms = pm.enumerator;
      while (perms.hasMoreElements()) {
        let permission = perms.getNext().QueryInterface(Ci.nsIPermission);
        if (permission.host == aHost.asciiHost && permission.type == type)
          aCallback(type);
      }
    }
  },

  updatePagePermissions: function updatePagePermissions() {
    let host = Browser.selectedBrowser.currentURI;
    let permissions = [];

    this._forEachPermissions(host, function(aType) {
      permissions.push(aType);
    });

    let lm = this._loginManager;
    if (!lm.getLoginSavingEnabled(host.prePath)) {
      permissions.push("password");
    }

    // Show the clear site preferences button if needed
    if (permissions.length) {
      let title = Elements.browserBundle.getString("pageactions.reset");
      let description = [];
      for each(permission in permissions)
        description.push(Elements.browserBundle.getString("pageactions." + permission));

      let node = this.appendItem(title, description.join(", "));
      node.onclick = function(event) {
        PageActions.clearPagePermissions();
        PageActions.removeItem(node);
      }
    }

    // Show the password button if needed
    let logins = lm.getAllLogins({});
    for each(login in logins) {
      if (login.hostname != host.prePath)
        continue;

      let title = Elements.browserBundle.getString("pageactions.password.forget");
      let node = this.appendItem(title, "");
      node.onclick = function(event) {
        lm.removeLogin(login);
        PageActions.removeItem(node);
      };
    }
  },

  clearPagePermissions: function clearPagePermissions() {
    let pm = this._permissionManager;
    let host = Browser.selectedBrowser.currentURI;
    this._forEachPermissions(host, function(aType) {
      pm.remove(host.asciiHost, aType);
    });

    let lm = this._loginManager;
    if (!lm.getLoginSavingEnabled(host.prePath))
      lm.setLoginSavingEnabled(host.prePath, true);
  },

  appendItem: function appendItem(aTitle, aDesc, aImage) {
    let container = document.getElementById("pageactions-container");
    let item = document.createElement("pageaction");
    item.setAttribute("title", aTitle);
    item.setAttribute("description", aDesc);
    if (aImage)
      item.setAttribute("image", aImage);
    container.appendChild(item);

    this.resize();
    container.hidden = !container.hasChildNodes();

    return item;
  },

  removeItem: function removeItem(aItem) {
    let container = document.getElementById("pageactions-container");
    container.removeChild(aItem);

    if (container.hasChildNodes())
      this.resize();
    container.hidden = !container.hasChildNodes();
  },

  removeAllItems: function removeAllItems() {
    let container = document.getElementById("pageactions-container");
    while(container.hasChildNodes())
      this.removeItem(container.lastChild);
  },

  resize: function resize() {
    let container = document.getElementById("pageactions-container");
    if (container.hidden)
      return;

    // We manually size the arrowscrollbox
    let childHeight = container.firstChild.getBoundingClientRect().height;
    let linesCount = (window.innerHeight < window.innerWidth) ? Math.round(container.childNodes.length / 2)
                                                              : container.childNodes.length;

    const kMargin = 64;
    let toolbarHeight = BrowserUI.toolbarH;
    let identityHeight = document.getElementById("identity-popup-container").getBoundingClientRect().height;
    let maxHeight = window.innerHeight - (toolbarHeight + identityHeight) - kMargin;

    let additional = 50; // size of the scroll arrows + margins
    container.style.height = Math.min(maxHeight, linesCount * childHeight + additional) + "px";
  }
}

var NewTabPopup = {
  _timeout: 0,
  _tabs: [],

  get box() {
    delete this.box;
    return this.box = document.getElementById("newtab-popup");
  },

  _updateLabel: function() {
    let newtabStrings = Elements.browserBundle.getString("newtabpopup.opened");
    let label = PluralForm.get(this._tabs.length, newtabStrings).replace("#1", this._tabs.length);

    this.box.firstChild.setAttribute("value", label);
  },

  hide: function() {
    if (this._timeout) {
      clearTimeout(this._timeout);
      this._timeout = 0;
    }

    this._tabs = [];
    this.box.hidden = true;
    BrowserUI.popPopup();
  },

  show: function(aTab) {
    BrowserUI.pushPopup(this, this.box);

    this._tabs.push(aTab);
    this._updateLabel();

    this.box.top = aTab.getBoundingClientRect().top + (aTab.getBoundingClientRect().height / 3);
    this.box.hidden = false;

    if (this._timeout)
      clearTimeout(this._timeout);

    this._timeout = setTimeout(function(self) {
      self.hide();
    }, 2000, this);
  },

  selectTab: function() {
    BrowserUI.selectTab(this._tabs.pop());
    this.hide();
  }
}

var BookmarkPopup = {
  get box() {
    delete this.box;
    return this.box = document.getElementById("bookmark-popup");
  },

  _bookmarkPopupTimeout: -1,

  hide : function hide() {
    if (this._bookmarkPopupTimeout != -1) {
      clearTimeout(this._bookmarkPopupTimeout);
      this._bookmarkPopupTimeout = -1;
    }
    this.box.hidden = true;
    BrowserUI.popPopup();
  },

  show : function show(aAutoClose) {
    const margin = 10;

    this.box.hidden = false;

    let [,,,controlsW] = Browser.computeSidebarVisibility();
    this.box.left = window.innerWidth - (this.box.getBoundingClientRect().width + controlsW + margin);
    this.box.top  = BrowserUI.starButton.getBoundingClientRect().top + margin;

    if (aAutoClose) {
      this._bookmarkPopupTimeout = setTimeout(function (self) {
        self._bookmarkPopupTimeout = -1;
        self.hide();
      }, 2000, this);
    }

    // include starButton here, so that click-to-dismiss works as expected
    BrowserUI.pushPopup(this, [this.box, BrowserUI.starButton]);
  },

  toggle : function toggle(aAutoClose) {
    if (this.box.hidden)
      this.show(aAutoClose);
    else
      this.hide();
  }
}

var BookmarkHelper = {
  _panel: null,
  _editor: null,

  edit: function BH_edit(aURI) {
    if (!aURI)
      aURI = getBrowser().currentURI;

    let itemId = PlacesUtils.getMostRecentBookmarkForURI(aURI);
    if (itemId == -1)
      return;

    let title = PlacesUtils.bookmarks.getItemTitle(itemId);
    let tags = PlacesUtils.tagging.getTagsForURI(aURI, {});

    const XULNS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
    this._editor = document.createElementNS(XULNS, "placeitem");
    this._editor.setAttribute("id", "bookmark-item");
    this._editor.setAttribute("flex", "1");
    this._editor.setAttribute("type", "bookmark");
    this._editor.setAttribute("ui", "manage");
    this._editor.setAttribute("title", title);
    this._editor.setAttribute("uri", aURI.spec);
    this._editor.setAttribute("itemid", itemId);
    this._editor.setAttribute("tags", tags.join(", "));
    this._editor.setAttribute("onclose", "BookmarkHelper.hide()");
    document.getElementById("bookmark-form").appendChild(this._editor);

    let toolbar = document.getElementById("toolbar-main");
    let top = toolbar.top + toolbar.boxObject.height;

    this._panel = document.getElementById("bookmark-container");
    this._panel.top = (top < 0 ? 0 : top);
    this._panel.hidden = false;
    BrowserUI.pushPopup(this, this._panel);

    let self = this;
    Browser.forceChromeReflow();
    self._editor.startEditing();
  },

  save: function BH_save() {
    this._editor.stopEditing(true);
  },

  hide: function BH_hide() {
    BrowserUI.updateStar();

    // Note: the _editor will have already saved the data, if needed, by the time
    // this method is called, since this method is called via the "close" event.
    this._editor.parentNode.removeChild(this._editor);
    this._editor = null;

    this._panel.hidden = true;
    BrowserUI.popPopup();
  },
};

var BookmarkList = {
  _panel: null,
  _bookmarks: null,
  _manageButtton: null,

  get mobileRoot() {
    let items = PlacesUtils.annotations.getItemsWithAnnotation("mobile/bookmarksRoot", {});
    if (!items.length)
      throw "Couldn't find mobile bookmarks root!";

    delete this.mobileRoot;
    return this.mobileRoot = items[0];
  },

  show: function() {
    this._panel = document.getElementById("bookmarklist-container");
    this._panel.width = window.innerWidth;
    this._panel.height = window.innerHeight;
    this._panel.hidden = false;
    BrowserUI.pushDialog(this);

    this._bookmarks = document.getElementById("bookmark-items");
    this._bookmarks.addEventListener("BookmarkRemove", this, true);
    this._bookmarks.manageUI = false;
    this._bookmarks.openFolder();

    this._manageButton = document.getElementById("tool-bookmarks-manage");
    this._manageButton.disabled = (this._bookmarks.items.length == 0);
  },

  close: function() {
    BrowserUI.updateStar();

    if (this._bookmarks.manageUI)
      this.toggleManage();
    this._bookmarks.blur();
    this._bookmarks.removeEventListener("BookmarkRemove", this, true);

    this._panel.hidden = true;
    BrowserUI.popDialog();
  },

  toggleManage: function() {
    this._bookmarks.manageUI = !(this._bookmarks.manageUI);
    this._manageButton.checked = this._bookmarks.manageUI;
  },

  openBookmark: function() {
    let item = this._bookmarks.activeItem;
    if (item.spec) {
      this.close();
      BrowserUI.goToURI(item.spec);
    }
  },

  handleEvent: function(aEvent) {
    if (aEvent.type == "BookmarkRemove") {
      if (this._bookmarks.isRootFolder && this._bookmarks.items.length == 1) {
        this._manageButton.disabled = true;
        this.toggleManage();
      }
    }
  }
};

var FormHelper = {
  _open: false,
  _nodes: null,
  get _container() {
    delete this._container;
    return this._container = document.getElementById("form-helper-container");
  },

  get _helperSpacer() {
    delete this._helperSpacer;
    return this._helperSpacer = document.getElementById("form-helper-spacer");
  },

  get _selectContainer() {
    delete this._selectContainer;
    return this._selectContainer = document.getElementById("select-container");
  },

  get _autofillContainer() {
    delete this._autofillContainer;
    return this._autofillContainer = document.getElementById("form-helper-autofill");
  },

  _getRectForElement: function formHelper_getRectForElement(aElement) {
    const kDistanceMax = 100;
    let elRect = Browser.getBoundingContentRect(aElement);
    let bv = Browser._browserView;

    let labels = this.getLabelsFor(aElement);
    for (let i=0; i<labels.length; i++) {
      let labelRect = Browser.getBoundingContentRect(labels[i]);
      if (labelRect.left < elRect.left) {
        let isClose = Math.abs(labelRect.left - elRect.left) - labelRect.width < kDistanceMax &&
                      Math.abs(labelRect.top - elRect.top) - labelRect.height < kDistanceMax;
        if (isClose) {
          let width = labelRect.width + elRect.width + (elRect.left - labelRect.left - labelRect.width);
          return new Rect(labelRect.left, labelRect.top, width, elRect.height).expandToIntegers();
        }
      }
    }
    return elRect;
  },

  _update: function(aPreviousElement, aNewElement) {
    this._updateSelect(aPreviousElement, aNewElement);

    // Setup autofill UI
    if (aNewElement instanceof HTMLInputElement && aNewElement.type == "text") {
      let suggestions = this._getSuggestions();
      this._setSuggestions(suggestions);
    } else {
      this._autofillContainer.hidden = true;
    }

    let height = Math.floor(this._container.getBoundingClientRect().height);
    this._container.top = window.innerHeight - height;

    document.getElementById("form-helper-previous").disabled = this._getPrevious() ? false : true;
    document.getElementById("form-helper-next").disabled = this._getNext() ? false : true;
  },

  _updateSelect: function(aPreviousElement, aNewElement) {
    let previousIsSelect = this._isValidSelectElement(aPreviousElement);
    let currentIsSelect = this._isValidSelectElement(aNewElement);

    if (currentIsSelect && !previousIsSelect) {
      this._selectContainer.height = window.innerHeight / 1.8;

      let rootNode = this._container;
      rootNode.insertBefore(this._selectContainer, rootNode.lastChild);

      SelectHelper.show(aNewElement);
    }
    else if (currentIsSelect && previousIsSelect) {
      SelectHelper.reset();
      SelectHelper.show(aNewElement);
    }
    else if (!currentIsSelect && previousIsSelect) {
      let rootNode = this._container.parentNode;
      rootNode.insertBefore(this._selectContainer, rootNode.lastChild);

      SelectHelper.close();
    }
  },

  _isValidElement: function(aElement) {
    if (aElement.disabled)
      return false;

    if (aElement.getAttribute("role") == "button" && aElement.hasAttribute("tabindex"))
      return this._isElementVisible(aElement);

    if (this._isValidSelectElement(aElement) || aElement instanceof HTMLTextAreaElement)
      return this._isElementVisible(aElement);

    if (aElement instanceof HTMLInputElement || aElement instanceof HTMLButtonElement) {
      let ignoreInputElements = ["checkbox", "radio", "hidden", "reset", "button"];
      let isValidElement = (ignoreInputElements.indexOf(aElement.type) == -1);
      if (!isValidElement)
        return false;

      return this._isElementVisible(aElement);
    }

    return false;
  },

  _isValidSelectElement: function(aElement) {
    return (aElement instanceof HTMLSelectElement) || (aElement instanceof Ci.nsIDOMXULMenuListElement);
  },

  _isElementVisible: function(aElement) {
    let style = aElement.ownerDocument.defaultView.getComputedStyle(aElement, null);
    let isVisible = (style.getPropertyValue("visibility") != "hidden");
    let isOpaque = (style.getPropertyValue("opacity") != 0);

    let rect = aElement.getBoundingClientRect();
    return isVisible && isOpaque && (rect.height != 0 || rect.width != 0);
  },

  _getAll: function() {
    let elements = [];

    // get all the documents
    let documents = [getBrowser().contentDocument];
    let iframes = getBrowser().contentDocument.querySelectorAll("iframe, frame");
    for (let i = 0; i < iframes.length; i++)
      documents.push(iframes[i].contentDocument);

    for (let i = 0; i < documents.length; i++) {
      let nodes = documents[i].querySelectorAll("input, button, select, textarea, [role=button]");

      for (let j =0; j < nodes.length; j++) {
        let node = nodes[j];
        if (this._isValidElement(node))
          elements.push(node);
      }
    }

    function orderByTabIndex(a, b) {
      // for an explanation on tabbing navigation see
      // http://www.w3.org/TR/html401/interact/forms.html#h-17.11.1
      // In resume tab index navigation order is 1, 2, 3, ..., 32767, 0
      if (a.tabIndex == 0 || b.tabIndex == 0)
        return b.tabIndex;

      return a.tabIndex > b.tabIndex;
    }
    return elements.sort(orderByTabIndex);
  },

  _getPrevious: function() {
    let index = this._nodes.indexOf(this._currentElement);
    return (index != -1 ? this._nodes[--index] : null);
  },

  _getNext: function() {
    let index = this._nodes.indexOf(this._currentElement);
    return (index != -1 ? this._nodes[++index] : null);
  },

  _fac: Cc["@mozilla.org/satchel/form-autocomplete;1"].getService(Ci.nsIFormAutoComplete),
  _getSuggestions: function() {
    let suggestions = [];
    let currentValue = this._currentElement.value;
    let results = this._fac.autoCompleteSearch(this._currentElement.name, currentValue, this._currentElement, null);
    if (results.matchCount > 0) {
      for (let i = 0; i < results.matchCount; i++) {
        let value = results.getValueAt(i);
        suggestions.push(value);
      }
    }

    return suggestions;
  },

  _setSuggestions: function(aSuggestions) {
    let autofill = this._autofillContainer;
    while (autofill.hasChildNodes())
      autofill.removeChild(autofill.lastChild);

    let fragment = document.createDocumentFragment();
    for (let i = 0; i < aSuggestions.length; i++) {
      let value = aSuggestions[i];
      let button = document.createElement("label");
      button.setAttribute("value", value);
      fragment.appendChild(button);
    }
    autofill.appendChild(fragment);
    autofill.hidden = !aSuggestions.length;
  },

  doAutoFill: function formHelperDoAutoFill(aElement) {
    if (!this._currentElement)
     return;

    // Suggestions are only in <label>s. Ignore the rest.
    if (aElement instanceof Ci.nsIDOMXULLabelElement)
      this._currentElement.value = aElement.value;
  },

  getLabelsFor: function(aElement) {
    let associatedLabels = [];
    if (this._isValidElement(aElement)) {
      let labels = aElement.ownerDocument.getElementsByTagName("label");
      for (let i=0; i<labels.length; i++) {
        if (labels[i].getAttribute("for") == aElement.id)
          associatedLabels.push(labels[i]);
      }
    }

    if (aElement.parentNode instanceof HTMLLabelElement)
      associatedLabels.push(aElement.parentNode);

    return associatedLabels.filter(this._isElementVisible);
  },

  _currentElement: null,
  getCurrentElement: function() {
    return this._currentElement;
  },

  setCurrentElement: function(aElement) {
    if (!aElement)
      return;

    let previousElement = this._currentElement;
    this._currentElement = aElement;
    this._update(previousElement, aElement);

    let containerHeight = this._container.getBoundingClientRect().height;
    this._helperSpacer.setAttribute("height", containerHeight);

    this.zoom(aElement);
    gFocusManager.setFocus(aElement, Ci.nsIFocusManager.FLAG_NOSCROLL);
  },

  goToPrevious: function formHelperGoToPrevious() {
    let previous = this._getPrevious();
    this.setCurrentElement(previous);
  },

  goToNext: function formHelperGoToNext() {
    let next = this._getNext();
    this.setCurrentElement(next);
  },

  open: function formHelperOpen(aElement) {
    if (this._open == true && aElement == this._currentElement &&
        gFocusManager.focusedElement == this._currentElement)
      return false;

    this._open = true;
    window.addEventListener("keyup", this, false);
    let bv = Browser._browserView;
    bv.ignorePageScroll(true);

    this._container.hidden = false;
    this._helperSpacer.hidden = false;

    this._nodes = this._getAll();
    this.setCurrentElement(aElement);
    return true;
  },

  close: function formHelperHide() {
    if (!this._open)
      return;

    this._updateSelect(this._currentElement, null);

    this._helperSpacer.hidden = true;

    // give the form spacer area back to the content
    let bv = Browser._browserView;
    Browser.forceChromeReflow();
    Browser.contentScrollboxScroller.scrollBy(0, 0);
    bv.onAfterVisibleMove();

    bv.ignorePageScroll(false);

    window.removeEventListener("keyup", this, false);
    this._container.hidden = true;
    this._currentElement = null;
    this._open = false;
  },

  handleEvent: function formHelperHandleEvent(aEvent) {
    let isChromeFocused = gFocusManager.getFocusedElementForWindow(window, false, {}) == gFocusManager.focusedElement;
    if (isChromeFocused)
      return;

    let currentElement = this.getCurrentElement();
    switch (aEvent.keyCode) {
      case aEvent.DOM_VK_DOWN:
        if (currentElement instanceof HTMLTextAreaElement) {
          let existSelection = currentElement.selectionEnd - currentElement.selectionStart;
          let isEnd = (currentElement.textLength == currentElement.selectionEnd);
          if (!isEnd || existSelection)
            return;
        }

        this.goToNext();
        break;

      case aEvent.DOM_VK_UP:
        if (currentElement instanceof HTMLTextAreaElement) {
          let existSelection = currentElement.selectionEnd - currentElement.selectionStart;
          let isStart = (currentElement.selectionEnd == 0);
          if (!isStart || existSelection)
            return;
        }

        this.goToPrevious();
        break;

      case aEvent.DOM_VK_RETURN:
        break;

      default:
        let target = aEvent.target;
        if (currentElement instanceof HTMLInputElement && currentElement.type == "text") {
          let suggestions = this._getSuggestions();
          this._setSuggestions(suggestions);

          let height = Math.floor(this._container.getBoundingClientRect().height);
          this._container.top = window.innerHeight - height;
          this._helperSpacer.setAttribute("height", height);
        
          // XXX if we are at the bottom of the page we need to give back the content
          // area by refreshing it
          if (suggestions.length == 0) {
            let bv = Browser._browserView;
            Browser.forceChromeReflow();
            Browser.contentScrollboxScroller.scrollBy(0, 0);
            bv.onAfterVisibleMove();
          }
        } else if (currentElement == target && this._isValidSelectElement(target)) {
          SelectHelper.unselectAll();
          SelectHelper.selectByIndex(target.selectedIndex);
        }
        break;
    }
  },

  zoom: function formHelperZoom(aElement) {
    let zoomLevel = Browser._getZoomLevelForElement(aElement);
    zoomLevel = Math.min(Math.max(kBrowserFormZoomLevelMin, zoomLevel), kBrowserFormZoomLevelMax);

    let elRect = this._getRectForElement(aElement);
    let zoomRect = Browser._getZoomRectForPoint(elRect.center().x, elRect.y, zoomLevel);

    Browser.setVisibleRect(zoomRect);
  },

  canShowUIFor: function(aElement) {
    if (!aElement)
      return false;

    // Some forms elements are valid in the sense that we want the Form
    // Assistant to stop on it, but we don't want it to display when
    // the user clicks on it
    let formExceptions = ["submit", "image", "file"];
    if (aElement instanceof HTMLInputElement && formExceptions.indexOf(aElement.type) != -1)
      return false;

    if (aElement instanceof HTMLButtonElement || (aElement.getAttribute("role") == "button" && aElement.hasAttribute("tabindex")))
      return false;

    return this._isValidElement(aElement);
  }
};

function SelectWrapper(aControl) {
  this._control = aControl;
}

SelectWrapper.prototype = {
  get selectedIndex() { return this._control.selectedIndex; },
  get multiple() { return this._control.multiple; },
  get options() { return this._control.options; },
  get children() { return this._control.children; },

  getText: function(aChild) { return aChild.text; },
  isOption: function(aChild) { return aChild instanceof HTMLOptionElement; },
  isGroup: function(aChild) { return aChild instanceof HTMLOptGroupElement; },
  select: function(aIndex, aSelected, aClearAll) {
    let selectElement = this._control.QueryInterface(Ci.nsISelectElement);
    selectElement.setOptionsSelectedByIndex(aIndex, aIndex, aSelected, aClearAll, false, true);
  },
  focus: function() { this._control.focus(); },
  fireOnChange: function() {
    let control = this._control;
    let evt = document.createEvent("Events");
    evt.initEvent("change", true, true, window, 0,
                  false, false,
                  false, false, null);
    control.dispatchEvent(evt);
  }
};

function MenulistWrapper(aControl) {
  this._control = aControl;
}

MenulistWrapper.prototype = {
  get selectedIndex() { return this._control.selectedIndex; },
  get multiple() { return false; },
  get options() { return this._control.menupopup.children; },
  get children() { return this._control.menupopup.children; },

  getText: function(aChild) { return aChild.label; },
  isOption: function(aChild) { return aChild instanceof Ci.nsIDOMXULSelectControlItemElement; },
  isGroup: function(aChild) { return false },
  select: function(aIndex, aSelected, aClearAll) {
    this._control.selectedIndex = aIndex;
  },
  focus: function() { this._control.focus(); },
  fireOnChange: function() {
    let control = this._control;
    let evt = document.createEvent("XULCommandEvent");
    evt.initCommandEvent("command", true, true, window, 0,
                         false, false,
                         false, false, null);
    control.dispatchEvent(evt);
  }
};

var SelectHelper = {
  _panel: null,
  _list: null,
  _control: null,
  _selectedIndexes: [],

  _getSelectedIndexes: function() {
    let indexes = [];
    let control = this._control;

    if (control.multiple) {
      for (let i = 0; i < control.options.length; i++) {
        if (control.options[i].selected)
          indexes.push(i);
      }
    }
    else {
      indexes.push(control.selectedIndex);
    }

    return indexes;
  },

  show: function(aControl) {
    if (!aControl)
      return;

    if (aControl instanceof HTMLSelectElement)
      this._control = new SelectWrapper(aControl);
    else if (aControl instanceof Ci.nsIDOMXULMenuListElement)
      this._control = new MenulistWrapper(aControl);
    else
      throw "Unknown list element";

    this._selectedIndexes = this._getSelectedIndexes();

    this._list = document.getElementById("select-list");
    this._list.setAttribute("multiple", this._control.multiple ? "true" : "false");

    let firstSelected = null;

    let optionIndex = 0;
    let children = this._control.children;
    for (let i=0; i<children.length; i++) {
      let child = children[i];
      if (this._control.isGroup(child)) {
        let group = document.createElement("option");
        group.setAttribute("label", child.label);
        this._list.appendChild(group);
        group.className = "optgroup";

        let subchildren = child.children;
        for (let ii=0; ii<subchildren.length; ii++) {
          let subchild = subchildren[ii];
          let item = document.createElement("option");
          item.setAttribute("label", this._control.getText(subchild));
          this._list.appendChild(item);
          item.className = "in-optgroup";
          item.optionIndex = optionIndex++;
          if (subchild.selected) {
            item.setAttribute("selected", "true");
            firstSelected = firstSelected ? firstSelected : item;
          }
        }
      } else if (this._control.isOption(child)) {
        let item = document.createElement("option");
        item.setAttribute("label", this._control.getText(child));
        this._list.appendChild(item);
        item.optionIndex = optionIndex++;
        if (child.selected) {
          item.setAttribute("selected", "true");
          firstSelected = firstSelected ? firstSelected : item;
        }
      }
    }

    this._panel = document.getElementById("select-container");
    this._panel.hidden = false;

    this._scrollElementIntoView(firstSelected);

    this._list.addEventListener("click", this, false);
  },

  _scrollElementIntoView: function(aElement) {
    if (!aElement)
      return;

    let index = -1;
    this._forEachOption(
      function(aItem, aIndex) {
        if (aElement.optionIndex == aItem.optionIndex)
          index = aIndex;
      }
    );

    if (index == -1)
      return;

    let scrollBoxObject = this._list.boxObject.QueryInterface(Ci.nsIScrollBoxObject);
    let itemHeight = aElement.getBoundingClientRect().height;
    let visibleItemsCount = this._list.boxObject.height / itemHeight;
    if ((index + 1) > visibleItemsCount) {
      let delta = Math.ceil(visibleItemsCount / 2);
      scrollBoxObject.scrollTo(0, ((index + 1) - delta) * itemHeight);
    }
    else {
      scrollBoxObject.scrollTo(0, 0);
    }
  },

  _forEachOption: function(aCallback) {
      let children = this._list.children;
      for (let i = 0; i < children.length; i++) {
        let item = children[i];
        if (!item.hasOwnProperty("optionIndex"))
          continue;
        aCallback(item, i);
      }
  },

  _updateControl: function() {
    let currentSelectedIndexes = this._getSelectedIndexes();

    let isIdentical = currentSelectedIndexes.length == this._selectedIndexes.length;
    if (isIdentical) {
      for (let i = 0; i < currentSelectedIndexes.length; i++) {
        if (currentSelectedIndexes[i] != this._selectedIndexes[i]) {
          isIdentical = false;
          break;
        }
      }
    }

    if (!isIdentical)
      this._control.fireOnChange();
  },

  reset: function() {
    this._updateControl();
    let empty = this._list.cloneNode(false);
    this._list.parentNode.replaceChild(empty, this._list);
    this._list = empty;
  },

  close: function() {
    this._list.removeEventListener("click", this, false);
    this._panel.hidden = true;

    this.reset();
  },

  unselectAll: function() {
    this._forEachOption(function(aItem, aIndex) aItem.selected = false);
  },

  selectByIndex: function(aIndex) {
    for (let i = 0; i < this._list.childNodes.length; i++) {
      let option = this._list.childNodes[i];
      if (option.optionIndex == aIndex) {
        option.selected = true;
        this._scrollElementIntoView(option);
        break;
      }
    }
  },

  handleEvent: function(aEvent) {
    switch (aEvent.type) {
      case "click":
        let item = aEvent.target;
        if (item && item.hasOwnProperty("optionIndex")) {
          if (this._control.multiple) {
            // Toggle the item state
            item.selected = !item.selected;
            this._control.select(item.optionIndex, item.selected, false);
          }
          else {
            this.unselectAll();

            // Select the new one and update the control
            item.selected = true;
            this._control.select(item.optionIndex, true, true);
          }
        }
        break;
    }
  }
};

const kXLinkNamespace = "http://www.w3.org/1999/xlink";

var ContextHelper = {
  popupNode: null,
  onLink: false,
  onImage: false,
  linkURL: "",
  mediaURL: "",

  _clearState: function ch_clearState() {
    this.popupNode = null;
    this.onLink = false;
    this.onImage = false;
    this.linkURL = "";
    this.mediaURL = "";
  },

  _getLinkURL: function ch_getLinkURL(aLink) {
    let href = aLink.href;  
    if (href)
      return href;

    href = aLink.getAttributeNS(kXLinkNamespace, "href");
    if (!href || !href.match(/\S/)) {
      // Without this we try to save as the current doc,
      // for example, HTML case also throws if empty
      throw "Empty href";
    }

    return Util.makeURLAbsolute(aLink.baseURI, href);
  },

  handleEvent: function ch_handleEvent(aEvent) {
    this._clearState();

    let [elementX, elementY] = Browser.transformClientToBrowser(aEvent.clientX, aEvent.clientY);
    this.popupNode = Browser.elementFromPoint(elementX, elementY);

    // Do checks for nodes that never have children.
    if (this.popupNode.nodeType == Node.ELEMENT_NODE) {
      // See if the user clicked on an image.
      if (this.popupNode instanceof Ci.nsIImageLoadingContent && this.popupNode.currentURI) {
        this.onImage = true;
        this.mediaURL = this.popupNode.currentURI.spec;
      }
    }

    let elem = this.popupNode;
    while (elem) {
      if (elem.nodeType == Node.ELEMENT_NODE) {
        // Link?
        if (!this.onLink &&
             ((elem instanceof HTMLAnchorElement && elem.href) ||
              (elem instanceof HTMLAreaElement && elem.href) ||
              elem instanceof HTMLLinkElement ||
              elem.getAttributeNS(kXLinkNamespace, "type") == "simple")) {
            
          // Target is a link or a descendant of a link.
          this.onLink = true;
          this.linkURL = this._getLinkURL(elem);
        }
      }

      elem = elem.parentNode;
    }

    let first = last = null;
    let commands = document.getElementById("context-commands");
    for (let i=0; i<commands.childElementCount; i++) {
      let command = commands.children[i];
      let type = command.getAttribute("type");
      command.removeAttribute("selector");
      if (type.indexOf("image") != -1 && this.onImage) {
        first = (first ? first : command);
        last = command;
        command.hidden = false;
        continue;
      } else if (type.indexOf("link") != -1 && this.onLink) {
        first = (first ? first : command);
        last = command;
        command.hidden = false;
        continue;
      }
      command.hidden = true;
    }

    if (!first) {
      this._clearState();
      return;
    }
    
    first.setAttribute("selector", "first-child");
    last.setAttribute("selector", "last-child");

    let label = document.getElementById("context-hint");
    if (this.onImage)
      label.value = this.mediaURL;
    if (this.onLink)
      label.value = this.linkURL;

    let container = document.getElementById("context-popup");
    container.hidden = false;

    let rect = container.getBoundingClientRect();
    let height = Math.min(rect.height, 0.75 * window.innerWidth);
    let width = Math.min(rect.width, 0.75 * window.innerWidth);

    container.height = height;
    container.width = width;
    container.top = (window.innerHeight - height) / 2;
    container.left = (window.innerWidth - width) / 2;

    BrowserUI.pushPopup(this, [container]);
  },
  
  hide: function ch_hide() {
    this._clearState();
    
    let container = document.getElementById("context-popup");
    container.hidden = true;

    BrowserUI.popPopup();
   }
 };

var ContextCommands = {
  openInNewTab: function cc_openInNewTab(aEvent) {
    Browser.addTab(ContextHelper.linkURL, false);
  },

  saveImage: function cc_saveImage(aEvent) {
    let doc = ContextHelper.popupNode.ownerDocument;
    saveImageURL(ContextHelper.mediaURL, null, "SaveImageTitle", false, false, doc.documentURIObject);
  }
}

function removeBookmarksForURI(aURI) {
  //XXX blargle xpconnect! might not matter, but a method on
  // nsINavBookmarksService that takes an array of items to
  // delete would be faster. better yet, a method that takes a URI!
  let itemIds = PlacesUtils.getBookmarksForURI(aURI);
  itemIds.forEach(PlacesUtils.bookmarks.removeItem);

  BrowserUI.updateStar();
}
