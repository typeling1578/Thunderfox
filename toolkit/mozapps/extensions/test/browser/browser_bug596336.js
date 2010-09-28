/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Tests that upgrading bootstrapped add-ons behaves correctly while the
// manager is open

var gManagerWindow;
var gCategoryUtilities;

function test() {
  waitForExplicitFinish();

  open_manager("addons://list/extension", function(aWindow) {
    gManagerWindow = aWindow;
    gCategoryUtilities = new CategoryUtilities(gManagerWindow);
    run_next_test();
  });
}

function end_test() {
  close_manager(gManagerWindow, function() {
    finish();
  });
}

function get_list_item_count() {
  let view = gManagerWindow.document.getElementById("view-port").selectedPanel;
  let listid = view.id == "search-view" ? "search-list" : "addon-list";
  let list = gManagerWindow.document.getElementById(listid);
  let count = list.childNodes.length;
  // Remove the show all results item
  if (view.id == "search-view")
    count--;

  return count;
}

function get_node(parent, anonid) {
  return parent.ownerDocument.getAnonymousElementByAttribute(parent, "anonid", anonid);
}

function get_class_node(parent, cls) {
  return parent.ownerDocument.getAnonymousElementByAttribute(parent, "class", cls);
}

function install_addon(aXpi, aCallback) {
  AddonManager.getInstallForURL(TESTROOT + "addons/" + aXpi + ".xpi",
                                function(aInstall) {
    aInstall.addListener({
      onInstallEnded: function(aInstall) {
        executeSoon(aCallback);
      }
    });
    aInstall.install();
  }, "application/x-xpinstall");
}

function check_addon(aAddon, version) {
  is(get_list_item_count(), 1, "Should be one item in the list");
  is(aAddon.version, version, "Add-on should have the right version");

  let item = get_addon_element(gManagerWindow, "addon1@tests.mozilla.org");
  ok(!!item, "Should see the add-on in the list");

  // Force XBL to apply
  item.clientTop;

  is(get_node(item, "version").value, version, "Version should be correct");

  if (aAddon.userDisabled)
    is_element_visible(get_class_node(item, "disabled-postfix"), "Disabled postfix should be hidden");
  else
    is_element_hidden(get_class_node(item, "disabled-postfix"), "Disabled postfix should be hidden");
}

// Install version 1 then upgrade to version 2 with the manager open
add_test(function() {
  install_addon("browser_bug596336_1", function() {
    AddonManager.getAddonByID("addon1@tests.mozilla.org", function(aAddon) {
      check_addon(aAddon, "1.0");
      ok(!aAddon.userDisabled, "Add-on should not be disabled");

      install_addon("browser_bug596336_2", function() {
        AddonManager.getAddonByID("addon1@tests.mozilla.org", function(aAddon) {
          check_addon(aAddon, "2.0");
          ok(!aAddon.userDisabled, "Add-on should not be disabled");

          aAddon.uninstall();

          is(get_list_item_count(), 0, "Should be no items in the list");

          run_next_test();
        });
      });
    });
  });
});

// Install version 1 mark it as disabled then upgrade to version 2 with the
// manager open
add_test(function() {
  install_addon("browser_bug596336_1", function() {
    AddonManager.getAddonByID("addon1@tests.mozilla.org", function(aAddon) {
      aAddon.userDisabled = true;
      check_addon(aAddon, "1.0");
      ok(aAddon.userDisabled, "Add-on should be disabled");

      install_addon("browser_bug596336_2", function() {
        AddonManager.getAddonByID("addon1@tests.mozilla.org", function(aAddon) {
          check_addon(aAddon, "2.0");
          ok(aAddon.userDisabled, "Add-on should be disabled");

          aAddon.uninstall();

          is(get_list_item_count(), 0, "Should be no items in the list");

          run_next_test();
        });
      });
    });
  });
});

// Install version 1 click the remove button and then upgrade to version 2 with
// the manager open
add_test(function() {
  install_addon("browser_bug596336_1", function() {
    AddonManager.getAddonByID("addon1@tests.mozilla.org", function(aAddon) {
      check_addon(aAddon, "1.0");
      ok(!aAddon.userDisabled, "Add-on should not be disabled");

      let item = get_addon_element(gManagerWindow, "addon1@tests.mozilla.org");
      EventUtils.synthesizeMouse(get_node(item, "remove-btn"), 2, 2, { }, gManagerWindow);

      // Force XBL to apply
      item.clientTop;

      ok(aAddon.userDisabled, "Add-on should be disabled");
      ok(!aAddon.pendingUninstall, "Add-on should not be pending uninstall");
      is_element_visible(get_class_node(item, "pending"), "Pending message should be visible");

      install_addon("browser_bug596336_2", function() {
        AddonManager.getAddonByID("addon1@tests.mozilla.org", function(aAddon) {
          check_addon(aAddon, "2.0");
          ok(!aAddon.userDisabled, "Add-on should not be disabled");

          aAddon.uninstall();

          is(get_list_item_count(), 0, "Should be no items in the list");

          run_next_test();
        });
      });
    });
  });
});

// Install version 1, disable it, click the remove button and then upgrade to
// version 2 with the manager open
add_test(function() {
  install_addon("browser_bug596336_1", function() {
    AddonManager.getAddonByID("addon1@tests.mozilla.org", function(aAddon) {
      aAddon.userDisabled = true;
      check_addon(aAddon, "1.0");
      ok(aAddon.userDisabled, "Add-on should be disabled");

      let item = get_addon_element(gManagerWindow, "addon1@tests.mozilla.org");
      EventUtils.synthesizeMouse(get_node(item, "remove-btn"), 2, 2, { }, gManagerWindow);

      // Force XBL to apply
      item.clientTop;

      ok(aAddon.userDisabled, "Add-on should be disabled");
      ok(!aAddon.pendingUninstall, "Add-on should not be pending uninstall");
      is_element_visible(get_class_node(item, "pending"), "Pending message should be visible");

      install_addon("browser_bug596336_2", function() {
        AddonManager.getAddonByID("addon1@tests.mozilla.org", function(aAddon) {
          check_addon(aAddon, "2.0");
          ok(aAddon.userDisabled, "Add-on should be disabled");

          aAddon.uninstall();

          is(get_list_item_count(), 0, "Should be no items in the list");

          run_next_test();
        });
      });
    });
  });
});
