/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * nsIContentSerializer implementation that can be used with an
 * nsIDocumentEncoder to convert a DOM into plaintext in a nice way
 * (eg for copy/paste as plaintext).
 */

#ifndef nsPlainTextSerializer_h__
#define nsPlainTextSerializer_h__

#include "nsIContentSerializer.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsILineBreaker.h"
#include "nsIContent.h"
#include "nsIAtom.h"
#include "nsIDocumentEncoder.h"
#include "nsTArray.h"

namespace mozilla {
namespace dom {
class Element;
} // namespace dom
} // namespace mozilla

class nsPlainTextSerializer : public nsIContentSerializer
{
public:
  nsPlainTextSerializer();
  virtual ~nsPlainTextSerializer();

  NS_DECL_ISUPPORTS

  // nsIContentSerializer
  NS_IMETHOD Init(uint32_t flags, uint32_t aWrapColumn,
                  const char* aCharSet, bool aIsCopying,
                  bool aIsWholeDocument);

  NS_IMETHOD AppendText(nsIContent* aText, int32_t aStartOffset,
                        int32_t aEndOffset, nsAString& aStr);
  NS_IMETHOD AppendCDATASection(nsIContent* aCDATASection,
                                int32_t aStartOffset, int32_t aEndOffset,
                                nsAString& aStr);
  NS_IMETHOD AppendProcessingInstruction(nsIContent* aPI,
                                         int32_t aStartOffset,
                                         int32_t aEndOffset,
                                         nsAString& aStr)  { return NS_OK; }
  NS_IMETHOD AppendComment(nsIContent* aComment, int32_t aStartOffset,
                           int32_t aEndOffset, nsAString& aStr)  { return NS_OK; }
  NS_IMETHOD AppendDoctype(nsIContent *aDoctype,
                           nsAString& aStr)  { return NS_OK; }
  NS_IMETHOD AppendElementStart(mozilla::dom::Element* aElement,
                                mozilla::dom::Element* aOriginalElement,
                                nsAString& aStr); 
  NS_IMETHOD AppendElementEnd(mozilla::dom::Element* aElement,
                              nsAString& aStr);
  NS_IMETHOD Flush(nsAString& aStr);

  NS_IMETHOD AppendDocumentStart(nsIDocument *aDocument,
                                 nsAString& aStr);

protected:
  nsresult GetAttributeValue(nsIAtom* aName, nsString& aValueRet);
  void AddToLine(const PRUnichar* aStringToAdd, int32_t aLength);
  void EndLine(bool softlinebreak, bool aBreakBySpace = false);
  void EnsureVerticalSpace(int32_t noOfRows);
  void FlushLine();
  void OutputQuotesAndIndent(bool stripTrailingSpaces=false);
  void Output(nsString& aString);
  void Write(const nsAString& aString);
  bool IsInPre();
  bool IsInOL();
  bool IsCurrentNodeConverted();
  bool MustSuppressLeaf();

  /**
   * Returns the local name of the element as an atom if the element is an
   * HTML element and the atom is a static atom. Otherwise, nullptr is returned.
   */
  static nsIAtom* GetIdForContent(nsIContent* aContent);
  nsresult DoOpenContainer(nsIAtom* aTag);
  nsresult DoCloseContainer(nsIAtom* aTag);
  nsresult DoAddLeaf(nsIAtom* aTag);
  void DoAddText(bool aIsWhitespace, const nsAString& aText);

  // Inlined functions
  inline bool MayWrap()
  {
    return mWrapColumn &&
      ((mFlags & nsIDocumentEncoder::OutputFormatted) ||
       (mFlags & nsIDocumentEncoder::OutputWrap));
  }

  inline bool DoOutput()
  {
    return mHeadLevel == 0;
  }

  // Stack handling functions
  bool GetLastBool(const nsTArray<bool>& aStack);
  void SetLastBool(nsTArray<bool>& aStack, bool aValue);
  void PushBool(nsTArray<bool>& aStack, bool aValue);
  bool PopBool(nsTArray<bool>& aStack);
  
protected:
  nsString         mCurrentLine;
  uint32_t         mHeadLevel;
  bool             mAtFirstColumn;

  // Handling of quoted text (for mail):
  // Quotes need to be wrapped differently from non-quoted text,
  // because quoted text has a few extra characters (e.g. ">> ")
  // which makes the line length longer.
  // Mail can represent quotes in different ways:
  // Not wrapped in any special tag (if mail.compose.wrap_to_window_width)
  // or in a <span>.
  bool             mDontWrapAnyQuotes;  // no special quote markers

  bool             mStructs;            // Output structs (pref)

  // If we've just written out a cite blockquote, we need to remember it
  // so we don't duplicate spaces before a <pre wrap> (which mail uses to quote
  // old messages).
  bool             mHasWrittenCiteBlockquote;

  int32_t          mIndent;
  // mInIndentString keeps a header that has to be written in the indent.
  // That could be, for instance, the bullet in a bulleted list.
  nsString         mInIndentString;
  int32_t          mCiteQuoteLevel;
  int32_t          mFlags;
  int32_t          mFloatingLines; // To store the number of lazy line breaks

  // The wrap column is how many standard sized chars (western languages)
  // should be allowed on a line. There could be less chars if the chars
  // are wider than latin chars of more if the chars are more narrow.
  uint32_t         mWrapColumn;

  // The width of the line as it will appear on the screen (approx.) 
  uint32_t         mCurrentLineWidth; 

  // Treat quoted text as though it's preformatted -- don't wrap it.
  // Having it on a pref is a temporary measure, See bug 69638.
  int32_t          mSpanLevel;


  int32_t          mEmptyLines; // Will be the number of empty lines before
                                // the current. 0 if we are starting a new
                                // line and -1 if we are in a line.

  bool             mInWhitespace;
  bool             mPreFormatted;
  bool             mStartedOutput; // we've produced at least a character

  // While handling a new tag, this variable should remind if any line break
  // is due because of a closing tag. Setting it to "TRUE" while closing the tags.
  // Hence opening tags are guaranteed to start with appropriate line breaks.
  bool             mLineBreakDue; 

  nsString         mURL;
  int32_t          mHeaderStrategy;    /* Header strategy (pref)
                                          0 = no indention
                                          1 = indention, increased with
                                              header level (default)
                                          2 = numbering and slight indention */
  int32_t          mHeaderCounter[7];  /* For header-numbering:
                                          Number of previous headers of
                                          the same depth and in the same
                                          section.
                                          mHeaderCounter[1] for <h1> etc. */

  nsRefPtr<mozilla::dom::Element> mElement;

  // For handling table rows
  nsAutoTArray<bool, 8> mHasWrittenCellsForRow;
  
  // Values gotten in OpenContainer that is (also) needed in CloseContainer
  nsAutoTArray<bool, 8> mIsInCiteBlockquote;

  // The output data
  nsAString*            mOutputString;

  // The tag stack: the stack of tags we're operating on, so we can nest.
  // The stack only ever points to static atoms, so they don't need to be
  // refcounted.
  nsIAtom**        mTagStack;
  uint32_t         mTagStackIndex;

  // Content in the stack above this index should be ignored:
  uint32_t          mIgnoreAboveIndex;

  // The stack for ordered lists
  int32_t         *mOLStack;
  uint32_t         mOLStackIndex;

  uint32_t         mULCount;

  nsString                     mLineBreak;
  nsCOMPtr<nsILineBreaker>     mLineBreaker;

  // Conveniance constant. It would be nice to have it as a const static
  // variable, but that causes issues with OpenBSD and module unloading.
  const nsString          kSpace;
};

nsresult
NS_NewPlainTextSerializer(nsIContentSerializer** aSerializer);

#endif
