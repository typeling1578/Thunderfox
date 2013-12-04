/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MacIOSurfaceTextureHostBasic.h"
#include "mozilla/layers/BasicCompositor.h"
#include "mozilla/gfx/MacIOSurface.h"

namespace mozilla {
namespace layers {

MacIOSurfaceTextureSourceBasic::MacIOSurfaceTextureSourceBasic(
                                BasicCompositor* aCompositor,
                                MacIOSurface* aSurface)
  : mCompositor(aCompositor)
  , mSurface(aSurface)
{}

MacIOSurfaceTextureSourceBasic::~MacIOSurfaceTextureSourceBasic()
{
}

gfx::IntSize
MacIOSurfaceTextureSourceBasic::GetSize() const
{
  return gfx::IntSize(mSurface->GetDevicePixelWidth(),
                      mSurface->GetDevicePixelHeight());
}

gfx::SurfaceFormat
MacIOSurfaceTextureSourceBasic::GetFormat() const
{
  return mSurface->HasAlpha() ? gfx::FORMAT_R8G8B8A8 : gfx::FORMAT_B8G8R8X8;
}

MacIOSurfaceTextureHostBasic::MacIOSurfaceTextureHostBasic(
    uint64_t aID,
    TextureFlags aFlags,
    const SurfaceDescriptorMacIOSurface& aDescriptor
)
  : TextureHost(aID, aFlags)
{
  mSurface = MacIOSurface::LookupSurface(aDescriptor.surface(),
                                         aDescriptor.scaleFactor(),
                                         aDescriptor.hasAlpha());
}

gfx::SourceSurface*
MacIOSurfaceTextureSourceBasic::GetSurface()
{
  if (!mSourceSurface) {
    mSourceSurface = mSurface->GetAsSurface();
  }
  return mSourceSurface;
}


bool
MacIOSurfaceTextureHostBasic::Lock()
{
  if (!mCompositor) {
    return false;
  }

  if (!mTextureSource) {
    mTextureSource = new MacIOSurfaceTextureSourceBasic(mCompositor, mSurface);
  }
  return true;
}

void
MacIOSurfaceTextureHostBasic::SetCompositor(Compositor* aCompositor)
{
  BasicCompositor* compositor = static_cast<BasicCompositor*>(aCompositor);
  mCompositor = compositor;
  if (mTextureSource) {
    mTextureSource->SetCompositor(compositor);
  }
}

gfx::SurfaceFormat
MacIOSurfaceTextureHostBasic::GetFormat() const {
  return mSurface->HasAlpha() ? gfx::FORMAT_R8G8B8A8 : gfx::FORMAT_B8G8R8X8;
}

gfx::IntSize
MacIOSurfaceTextureHostBasic::GetSize() const {
  return gfx::IntSize(mSurface->GetDevicePixelWidth(),
                      mSurface->GetDevicePixelHeight());
}

}
}
