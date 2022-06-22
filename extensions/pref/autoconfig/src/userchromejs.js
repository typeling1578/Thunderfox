// skip 1st line
try {
  // Legacy add-on support is currently disabled due to remaining security concerns.
  // Cu.import('chrome://userchromejs/content/BootstrapLoader.jsm');
} catch (ex) {};

try {
  Cu.import('chrome://userchromejs/content/userChrome.jsm');
} catch (ex) {};
