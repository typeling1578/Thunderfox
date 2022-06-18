/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* global ExtensionAPI, ExtensionCommon, Services, XPCOMUtils */

this.browserLangSettings = class extends ExtensionAPI {
  getAPI(context) {
    const EventManager = ExtensionCommon.EventManager;
    
    return {
      browserLangSettings: {
        async setAppLangToSystemLang() {
          let systemLocale = Cc["@mozilla.org/intl/ospreferences;1"].getService(Ci.mozIOSPreferences).systemLocale;
          let systemLocale_obj = new Services.intl.Locale(systemLocale);
          let availableLocales = Services.locale.availableLocales;
          if (availableLocales.includes(systemLocale_obj.baseName)) {
            Services.locale.requestedLocales = [systemLocale_obj.baseName];
          } else if (availableLocales.includes(systemLocale_obj.language)) {
            Services.locale.requestedLocales = [systemLocale_obj.language];
          }
        },
      },
    };
  }
};
