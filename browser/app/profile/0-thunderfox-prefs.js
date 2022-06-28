#filter dumbComments emptyLines substitution

// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Non-static prefs that are specific to desktop Firefox belong in this file
// (unless there is a compelling and documented reason for them to belong in
// another file).
//
// Please indent all prefs defined within #ifdef/#ifndef conditions. This
// improves readability, particular for conditional blocks that exceed a single
// screen.

#ifdef XP_UNIX
  #ifndef XP_MACOSX
    #define UNIX_BUT_NOT_MAC
  #endif
#endif

pref("breakpad.reportURL", "");
pref("browser.tabs.crashReporting.sendReport", false);
pref("app.shield.optoutstudies.enabled", false);
pref("extensions.webcompat-reporter.enabled", false);
pref("datareporting.policy.dataSubmissionEnabled", false);
pref("datareporting.healthreport.uploadEnabled", false);
pref("toolkit.telemetry.enabled", false);
pref("toolkit.telemetry.archive.enabled", false);
pref("toolkit.telemetry.bhrPing.enabled", false);
pref("toolkit.telemetry.firstShutdownPing.enabled", false);
pref("toolkit.telemetry.geckoview.streaming", false);
pref("toolkit.telemetry.newProfilePing.enabled", false);
pref("toolkit.telemetry.pioneer-new-studies-available", false);
pref("toolkit.telemetry.reportingpolicy.firstRun", false);
pref("toolkit.telemetry.shutdownPingSender.enabled", false);
pref("toolkit.telemetry.shutdownPingSender.enabledFirstSession", false);
pref("toolkit.telemetry.testing.overrideProductsCheck", false);
pref("toolkit.telemetry.unified", false);
pref("toolkit.telemetry.updatePing.enabled", false);
pref("beacon.enabled", false);
pref("app.normandy.enabled", false);
pref("app.normandy.first_run", false);
pref("dom.ipc.plugins.flash.subprocess.crashreporter.enabled", false);
pref("browser.ping-centre.telemetry", false);
pref("browser.send_pings", false);

pref("geo.provider.ms-windows-location", false);
pref("geo.provider.network.url", "https://location.services.mozilla.com/v1/geolocate?key=%MOZILLA_API_KEY%");

pref("dom.battery.enabled", false);
pref("device.sensors.enabled", false);
pref("media.peerconnection.ice.default_address_only", true);

pref("svg.context-properties.content.enabled", true);
pref("layout.css.backdrop-filter.enabled", true);
pref("layout.css.color-mix.enabled", true);

pref("browser.startup.page", 3);
pref("browser.tabs.closeWindowWithLastTab", false);
pref("browser.tabs.warnOnClose", true);
pref("privacy.userContext.enabled", true);
pref("privacy.userContext.ui.enabled", true);

pref("browser.urlbar.update2.engineAliasRefresh", true);
pref("browser.search.separatePrivateDefault.ui.enabled", true);
pref("xpinstall.signatures.required", false);
pref("extensions.webextensions.restrictedDomains", "");
pref("xpinstall.userActivation.required", false);
pref("extensions.install_origins.enabled", false);
pref("browser.theme.colorway-closet", true);
pref("widget.disable-swipe-tracker", false);

pref("font.name-list.emoji", "Twemoji Mozilla");
pref("browser.preferences.moreFromMozilla", false);

pref("browser.urlbar.trimURLs", false);

pref("gfx.webrender.all", true);

pref("alerts.playSound", true);

pref("extensions.experiments.enabled", false, locked);
pref("app.update.langpack.enabled", false);
pref("extensions.getAddons.langpacks.url", "");

pref("privacy.restrict3rdpartystorage.rollout.preferences.TCPToggleInStandard", true);
pref("privacy.restrict3rdpartystorage.rollout.enabledByDefault", true);

pref("app.feedback.baseURL", "https://github.com/typeling1578/Thunderfox/issues");
