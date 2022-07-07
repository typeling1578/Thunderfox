// skip 1st line
{
  const { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm");

  try {
    if (Services.prefs.getBoolPref("extensions.experiments.legacyAddons.enabled", false)) {
      Cu.import('chrome://userchromejs/content/BootstrapLoader.jsm');
    }
  } catch (ex) {};

  try {
    if (Services.prefs.getBoolPref("toolkit.legacyUserProfileCustomizations.javascript", false)) {
      Cu.import('chrome://userchromejs/content/userChrome.jsm');
    }
  } catch (ex) {};
}
