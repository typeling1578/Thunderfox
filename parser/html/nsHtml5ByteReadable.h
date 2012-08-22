/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 
#ifndef nsHtml5ByteReadable_h__
#define nsHtml5ByteReadable_h__

#include "prtypes.h"

/**
 * A weak reference wrapper around a byte array.
 */
class nsHtml5ByteReadable
{
  public:

    nsHtml5ByteReadable(const uint8_t* current, const uint8_t* end)
     : current(current),
       end(end)
    {
    }

    inline int32_t read() {
      if (current < end) {
        return *(current++);
      } else {
        return -1;
      }
    }

  private:
    const uint8_t* current;
    const uint8_t* end;
};
#endif
