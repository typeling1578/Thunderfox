/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/
*/
/* Test inclusion of previous build ID in telemetry pings when build ID changes.
 * bug 841028
 *
 * Cases to cover:
 * 1) Run with no "previousBuildID" stored in prefs:
 *     -> no previousBuildID in telemetry system info, new value set in prefs.
 * 2) previousBuildID in prefs, equal to current build ID:
 *     -> no previousBuildID in telemetry, prefs not updated.
 * 3) previousBuildID in prefs, not equal to current build ID:
 *     -> previousBuildID in telemetry, new value set in prefs.
 */

"use strict"

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm", this);
Cu.import("resource://gre/modules/TelemetryPing.jsm", this);
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyGetter(this, "gDatareportingService",
  () => Cc["@mozilla.org/datareporting/service;1"]
          .getService(Ci.nsISupports)
          .wrappedJSObject);

// Force the Telemetry enabled preference so that TelemetryPing.reset() doesn't exit early.
Services.prefs.setBoolPref(TelemetryPing.Constants.PREF_ENABLED, true);

// Set up our dummy AppInfo object so we can control the appBuildID.
Cu.import("resource://testing-common/AppInfo.jsm", this);
updateAppInfo();

// Check that when run with no previous build ID stored, we update the pref but do not
// put anything into the metadata.
add_task(function* test_firstRun() {
  yield TelemetryPing.reset();
  let metadata = TelemetryPing.getMetadata();
  do_check_false("previousBuildID" in metadata);
  let appBuildID = getAppInfo().appBuildID;
  let buildIDPref = Services.prefs.getCharPref(TelemetryPing.Constants.PREF_PREVIOUS_BUILDID);
  do_check_eq(appBuildID, buildIDPref);
});

// Check that a subsequent run with the same build ID does not put prev build ID in
// metadata. Assumes testFirstRun() has already been called to set the previousBuildID pref.
add_task(function* test_secondRun() {
  yield TelemetryPing.reset();
  let metadata = TelemetryPing.getMetadata();
  do_check_false("previousBuildID" in metadata);
});

// Set up telemetry with a different app build ID and check that the old build ID
// is returned in the metadata and the pref is updated to the new build ID.
// Assumes testFirstRun() has been called to set the previousBuildID pref.
const NEW_BUILD_ID = "20130314";
add_task(function* test_newBuild() {
  let info = getAppInfo();
  let oldBuildID = info.appBuildID;
  info.appBuildID = NEW_BUILD_ID;
  yield TelemetryPing.reset();
  let metadata = TelemetryPing.getMetadata();
  do_check_eq(metadata.previousBuildID, oldBuildID);
  let buildIDPref = Services.prefs.getCharPref(TelemetryPing.Constants.PREF_PREVIOUS_BUILDID);
  do_check_eq(NEW_BUILD_ID, buildIDPref);
});


function run_test() {
  // Make sure we have a profile directory.
  do_get_profile();

  // Send the needed startup notifications to the datareporting service
  // to ensure that it has been initialized.
  gDatareportingService.observe(null, "app-startup", null);
  gDatareportingService.observe(null, "profile-after-change", null);

  run_next_test();
}
