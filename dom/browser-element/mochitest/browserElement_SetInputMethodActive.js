/* Any copyright is dedicated to the public domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Bug 905573 - Add setInputMethodActive to browser elements to allow gaia
// system set the active IME app.
'use strict';

SimpleTest.waitForExplicitFinish();
browserElementTestHelpers.setEnabledPref(true);
browserElementTestHelpers.addPermission();

function setup() {
  let appInfo = SpecialPowers.Cc['@mozilla.org/xre/app-info;1']
                .getService(SpecialPowers.Ci.nsIXULAppInfo);
  if (appInfo.name != 'B2G') {
    SpecialPowers.Cu.import("resource://gre/modules/Keyboard.jsm", window);
  }

  SpecialPowers.setBoolPref("dom.mozInputMethod.enabled", true);
  SpecialPowers.setBoolPref("dom.mozInputMethod.testing", true);
  SpecialPowers.addPermission('input-manage', true, document);
}

function tearDown() {
  SpecialPowers.setBoolPref("dom.mozInputMethod.enabled", false);
  SpecialPowers.setBoolPref("dom.mozInputMethod.testing", false);
  SpecialPowers.removePermission('input-manage', document);
  SimpleTest.finish();
}

function runTest() {
  let path = location.pathname;
  let imeUrl = location.protocol + '//' + location.host +
               path.substring(0, path.lastIndexOf('/')) +
               '/file_inputmethod.html';
  SpecialPowers.pushPermissions([{
    type: 'input',
    allow: true,
    context: {
      url: imeUrl,
      appId: SpecialPowers.Ci.nsIScriptSecurityManager.NO_APP_ID,
      isInBrowserElement: true
    }
  }], SimpleTest.waitForFocus.bind(SimpleTest, createFrames));
}

var gFrames = [];
var gInputFrame;

function createFrames() {
  // Create two input method iframes.
  let loadendCount = 0;
  let countLoadend = function() {
    if (this === gInputFrame) {
      // The frame script running in the frame where the input is hosted.
      let appFrameScript = function appFrameScript() {
        let input = content.document.body.firstElementChild;
        input.oninput = function() {
          sendAsyncMessage('test:InputMethod:oninput', {
            from: 'input',
            value: this.value
          });
        };

        input.onblur = function() {
          // "Expected" lost of focus since the test is finished.
          if (input.value === '#0#1hello') {
            return;
          }

          sendAsyncMessage('test:InputMethod:oninput', {
            from: 'input',
            error: true,
            value: 'Unexpected lost of focus on the input frame!'
          });
        };

        input.focus();
      }

      // Inject frame script to receive input.
      let mm = SpecialPowers.getBrowserFrameMessageManager(gInputFrame);
      mm.loadFrameScript('data:,(' + encodeURIComponent(appFrameScript.toString()) + ')();', false);
      mm.addMessageListener("test:InputMethod:oninput", next);
    } else {
      ok(this.setInputMethodActive, 'Can access setInputMethodActive.');

      // The frame script running in the input method frames.

      let appFrameScript = function appFrameScript() {
        content.addEventListener("message", function(evt) {
          sendAsyncMessage('test:InputMethod:imFrameMessage', {
            from: 'im',
            value: evt.data
          });
        });
      }

      // Inject frame script to receive message.
      let mm = SpecialPowers.getBrowserFrameMessageManager(this);
      mm.loadFrameScript('data:,(' + appFrameScript.toString() + ')();', false);
      mm.addMessageListener("test:InputMethod:imFrameMessage", next);
    }

    loadendCount++;
    if (loadendCount === 3) {
      startTest();
    }
  };

  // Create an input field to receive string from input method iframes.
  gInputFrame = document.createElement('iframe');
  SpecialPowers.wrap(gInputFrame).mozbrowser = true;
  gInputFrame.src =
    'data:text/html,<input autofocus value="hello" />' +
    '<p>This is targetted mozbrowser frame.</p>';
  document.body.appendChild(gInputFrame);
  gInputFrame.addEventListener('mozbrowserloadend', countLoadend);

  for (let i = 0; i < 2; i++) {
    let frame = gFrames[i] = document.createElement('iframe');
    SpecialPowers.wrap(gFrames[i]).mozbrowser = true;
    // When the input method iframe is activated, it will send the URL
    // hash to current focused element. We set different hash to each
    // iframe so that iframes can be differentiated by their hash.
    frame.src = 'file_inputmethod.html#' + i;
    document.body.appendChild(frame);
    frame.addEventListener('mozbrowserloadend', countLoadend);
  }
}

function startTest() {
  // Set focus to the input field and wait for input methods' inputting.
  SpecialPowers.DOMWindowUtils.focus(gInputFrame);

  let req0 = gFrames[0].setInputMethodActive(true);
  req0.onsuccess = function() {
    ok(true, 'setInputMethodActive succeeded (0).');
  };

  req0.onerror = function() {
    ok(false, 'setInputMethodActive failed (0): ' + this.error.name);
  };
}

var gCount = 0;

function next(msg) {
  let wrappedMsg = SpecialPowers.wrap(msg);
  let from = wrappedMsg.data.from;
  let value = wrappedMsg.data.value;

  if (wrappedMsg.data.error) {
    ok(false, wrappedMsg.data.value);

    return;
  }

  gCount++;

  // The texts sent from the first and the second input method are '#0' and
  // '#1' respectively.
  switch (gCount) {
    case 1:
      is(from, 'im', 'Message sequence unexpected (1).');
      is(value, '#0true', 'First frame should get the context first.');
      // Do nothing and wait for the input to show up in input frame.
      break;

    case 2:
      is(from, 'input', 'Message sequence unexpected (2).');
      is(value, '#0hello',
         'Failed to get correct input from the first iframe.');

      let req0 = gFrames[0].setInputMethodActive(false);
      req0.onsuccess = function() {
        ok(true, 'setInputMethodActive succeeded (0).');
      };
      req0.onerror = function() {
        ok(false, 'setInputMethodActive failed (0): ' + this.error.name);
      };
      let req1 = gFrames[1].setInputMethodActive(true);
      req1.onsuccess = function() {
       ok(true, 'setInputMethodActive succeeded (1).');
      };
      req1.onerror = function() {
       ok(false, 'setInputMethodActive failed (1): ' + this.error.name);
      };
      break;

    case 3:
      is(from, 'im', 'Message sequence unexpected (3).');
      is(value, '#0false', 'First frame should have the context removed.');
      // Do nothing and wait for the second frame to get the context;
      break;

    case 4:
      is(from, 'im', 'Message sequence unexpected (4).');
      is(value, '#1true', 'Second frame should get the context.');
      // Do nothing and wait for the input to show up in input frame.
      break;

    case 5:
      is(from, 'input', 'Message sequence unexpected (5).');
      is(value, '#0#1hello',
         'Failed to get correct input from the second iframe.');

      // Receive the second input from the second iframe.
      // Deactive the second iframe.
      let req3 = gFrames[1].setInputMethodActive(false);
      req3.onsuccess = function() {
        ok(true, 'setInputMethodActive(false) succeeded (3).');
      };
      req3.onerror = function() {
        ok(false, 'setInputMethodActive(false) failed (3): ' + this.error.name);
      };
      break;

    case 6:
      is(from, 'im', 'Message sequence unexpected (6).');
      is(value, '#1false', 'Second frame should have the context removed.');

      tearDown();
      break;
  }
}

setup();
addEventListener('testready', runTest);
