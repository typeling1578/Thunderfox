/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://www.whatwg.org/specs/web-apps/current-work/#the-option-element
 *
 * © Copyright 2004-2011 Apple Computer, Inc., Mozilla Foundation, and
 * Opera Software ASA. You are granted a license to use, reproduce
 * and create derivative works of this document.
 */

/* TODO: Bug 842276
 * [NamedConstructor=Option(),
 *  NamedConstructor=Option(DOMString text),
 *  NamedConstructor=Option(DOMString text, DOMString value),
 *  NamedConstructor=Option(DOMString text, DOMString value, boolean defaultSelected),
 *  NamedConstructor=Option(DOMString text, DOMString value, boolean defaultSelected, boolean selected)]
 */
interface HTMLOptionElement : HTMLElement {
           [SetterThrows]
           attribute boolean disabled;
  readonly attribute HTMLFormElement? form;
           [SetterThrows]
           attribute DOMString label;
           [SetterThrows]
           attribute boolean defaultSelected;
           [SetterThrows]
           attribute boolean selected;
           [SetterThrows]
           attribute DOMString value;

           [SetterThrows]
           attribute DOMString text;
           [GetterThrows]
  readonly attribute long index;
};
