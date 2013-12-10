/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define _USE_MATH_DEFINES

#include <cmath>
#include "FilterNodeSoftware.h"
#include "2D.h"
#include "Tools.h"
#include "Blur.h"
#include <map>
#include "FilterProcessing.h"
#include "mozilla/PodOperations.h"

// #define DEBUG_DUMP_SURFACES

#ifdef DEBUG_DUMP_SURFACES
#include "gfxImageSurface.h"
#include "gfx2DGlue.h"
namespace mozilla {
namespace gfx {
static void
DumpAsPNG(SourceSurface* aSurface)
{
  RefPtr<DataSourceSurface> dataSource = aSurface->GetDataSurface();
  IntSize size = dataSource->GetSize();
  nsRefPtr<gfxImageSurface> imageSurface =
    new gfxImageSurface(dataSource->GetData(), gfxIntSize(size.width, size.height),
                        dataSource->Stride(), SurfaceFormatToImageFormat(aSurface->GetFormat()));
  imageSurface->PrintAsDataURL();
}
} // namespace gfx
} // namespace mozilla
#endif

namespace mozilla {
namespace gfx {

namespace {

/**
 * This class provides a way to get a pow() results in constant-time. It works
 * by caching 256 values for bases between 0 and 1 and a fixed exponent.
 **/
class PowCache
{
public:
  PowCache()
  {
    CacheForExponent(0.0f);
  }

  void CacheForExponent(Float aExponent)
  {
    mExponent = aExponent;
    int numPreSquares = 0;
    while (numPreSquares < 5 && mExponent > (1 << (numPreSquares + 2))) {
      numPreSquares++;
    }
    mNumPowTablePreSquares = numPreSquares;
    for (int i = 0; i < sCacheSize; i++) {
      Float a = i / Float(sCacheSize - 1);
      for (int j = 0; j < mNumPowTablePreSquares; j++) {
        a = sqrt(a);
      }
      mPowTable[i] = uint16_t(pow(a, mExponent) * (1 << sOutputIntPrecisionBits));
    }
  }

  uint16_t Pow(uint16_t aBase)
  {
    // Results should be similar to what the following code would produce:
    // double x = double(aBase) / (1 << sInputIntPrecisionBits);
    // return uint16_t(pow(x, mExponent) * (1 << sOutputIntPrecisionBits));

    uint32_t a = aBase;
    for (int j = 0; j < mNumPowTablePreSquares; j++) {
      a = a * a >> sInputIntPrecisionBits;
    }
    static_assert(sCacheSize == (1 << sInputIntPrecisionBits >> 7), "please fix index calculation below");
    int i = a >> 7;
    return mPowTable[i];
  }

private:
  static const int sCacheSize = 256;
  static const int sInputIntPrecisionBits = 15;
  static const int sOutputIntPrecisionBits = 8;

  Float mExponent;
  int mNumPowTablePreSquares;
  uint16_t mPowTable[sCacheSize];
};

class PointLightSoftware
{
public:
  bool SetAttribute(uint32_t aIndex, Float) { return false; }
  bool SetAttribute(uint32_t aIndex, const Point3D &);
  void Prepare() {}
  Point3D GetVectorToLight(const Point3D &aTargetPoint);
  uint32_t GetColor(uint32_t aLightColor, const Point3D &aVectorToLight);

private:
  Point3D mPosition;
};

class SpotLightSoftware
{
public:
  SpotLightSoftware();
  bool SetAttribute(uint32_t aIndex, Float);
  bool SetAttribute(uint32_t aIndex, const Point3D &);
  void Prepare();
  Point3D GetVectorToLight(const Point3D &aTargetPoint);
  uint32_t GetColor(uint32_t aLightColor, const Point3D &aVectorToLight);

private:
  Point3D mPosition;
  Point3D mPointsAt;
  Point3D mVectorFromFocusPointToLight;
  Float mSpecularFocus;
  Float mLimitingConeAngle;
  Float mLimitingConeCos;
  PowCache mPowCache;
};

class DistantLightSoftware
{
public:
  DistantLightSoftware();
  bool SetAttribute(uint32_t aIndex, Float);
  bool SetAttribute(uint32_t aIndex, const Point3D &) { return false; }
  void Prepare();
  Point3D GetVectorToLight(const Point3D &aTargetPoint);
  uint32_t GetColor(uint32_t aLightColor, const Point3D &aVectorToLight);

private:
  Float mAzimuth;
  Float mElevation;
  Point3D mVectorToLight;
};

class DiffuseLightingSoftware
{
public:
  DiffuseLightingSoftware();
  bool SetAttribute(uint32_t aIndex, Float);
  void Prepare() {}
  uint32_t LightPixel(const Point3D &aNormal, const Point3D &aVectorToLight,
                      uint32_t aColor);

private:
  Float mDiffuseConstant;
};

class SpecularLightingSoftware
{
public:
  SpecularLightingSoftware();
  bool SetAttribute(uint32_t aIndex, Float);
  void Prepare();
  uint32_t LightPixel(const Point3D &aNormal, const Point3D &aVectorToLight,
                      uint32_t aColor);

private:
  Float mSpecularConstant;
  Float mSpecularExponent;
  uint32_t mSpecularConstantInt;
  PowCache mPowCache;
};

} // unnamed namespace

// from xpcom/ds/nsMathUtils.h
static int32_t
NS_lround(double x)
{
  return x >= 0.0 ? int32_t(x + 0.5) : int32_t(x - 0.5);
}

void
ClearDataSourceSurface(DataSourceSurface *aSurface)
{
  size_t numBytes = aSurface->GetSize().height * aSurface->Stride();
  uint8_t* data = aSurface->GetData();
  PodZero(data, numBytes);
}

static ptrdiff_t
DataOffset(DataSourceSurface* aSurface, IntPoint aPoint)
{
  return aPoint.y * aSurface->Stride() +
         aPoint.x * BytesPerPixel(aSurface->GetFormat());
}

/**
 * aSrcRect: Rect relative to the aSrc surface
 * aDestPoint: Point inside aDest surface
 */
static void
CopyRect(DataSourceSurface* aSrc, DataSourceSurface* aDest,
         IntRect aSrcRect, IntPoint aDestPoint)
{
  MOZ_ASSERT(aSrc->GetFormat() == aDest->GetFormat(), "different surface formats");
  MOZ_ASSERT(IntRect(IntPoint(), aSrc->GetSize()).Contains(aSrcRect), "source rect too big for source surface");
  MOZ_ASSERT(IntRect(IntPoint(), aDest->GetSize()).Contains(aSrcRect - aSrcRect.TopLeft() + aDestPoint), "dest surface too small");
  uint8_t* sourceData = aSrc->GetData();
  uint32_t sourceStride = aSrc->Stride();
  uint8_t* destData = aDest->GetData();
  uint32_t destStride = aDest->Stride();

  sourceData += DataOffset(aSrc, aSrcRect.TopLeft());
  destData += DataOffset(aDest, aDestPoint);

  if (BytesPerPixel(aSrc->GetFormat()) == 4) {
    for (int32_t y = 0; y < aSrcRect.height; y++) {
      PodCopy((int32_t*)destData, (int32_t*)sourceData, aSrcRect.width);
      sourceData += sourceStride;
      destData += destStride;
    }
  } else if (BytesPerPixel(aSrc->GetFormat()) == 1) {
    for (int32_t y = 0; y < aSrcRect.height; y++) {
      PodCopy(destData, sourceData, aSrcRect.width);
      sourceData += sourceStride;
      destData += destStride;
    }
  }
}

TemporaryRef<DataSourceSurface>
CloneAligned(DataSourceSurface* aSource)
{
  RefPtr<DataSourceSurface> copy =
    Factory::CreateDataSourceSurface(aSource->GetSize(), aSource->GetFormat());
  CopyRect(aSource, copy, IntRect(IntPoint(), aSource->GetSize()), IntPoint());
  return copy;
}

static void
FillRectWithPixel(DataSourceSurface *aSurface, const IntRect &aFillRect, IntPoint aPixelPos)
{
  uint8_t* data = aSurface->GetData();
  uint8_t* sourcePixelData = data + DataOffset(aSurface, aPixelPos);
  int32_t stride = aSurface->Stride();
  data += DataOffset(aSurface, aFillRect.TopLeft());
  int bpp = BytesPerPixel(aSurface->GetFormat());

  // Fill the first row by hand.
  if (bpp == 4) {
    uint32_t sourcePixel = *(uint32_t*)sourcePixelData;
    for (int32_t x = 0; x < aFillRect.width; x++) {
      *((uint32_t*)data + x) = sourcePixel;
    }
  } else if (BytesPerPixel(aSurface->GetFormat()) == 1) {
    uint8_t sourcePixel = *sourcePixelData;
    memset(data, sourcePixel, aFillRect.width);
  }

  // Copy the first row into the other rows.
  for (int32_t y = 1; y < aFillRect.height; y++) {
    PodCopy(data + y * stride, data, aFillRect.width * bpp);
  }
}

static void
FillRectWithVerticallyRepeatingHorizontalStrip(DataSourceSurface *aSurface,
                                               const IntRect &aFillRect,
                                               const IntRect &aSampleRect)
{
  uint8_t* data = aSurface->GetData();
  int32_t stride = aSurface->Stride();
  uint8_t* sampleData = data + DataOffset(aSurface, aSampleRect.TopLeft());
  data += DataOffset(aSurface, aFillRect.TopLeft());
  if (BytesPerPixel(aSurface->GetFormat()) == 4) {
    for (int32_t y = 0; y < aFillRect.height; y++) {
      PodCopy((uint32_t*)data, (uint32_t*)sampleData, aFillRect.width);
      data += stride;
    }
  } else if (BytesPerPixel(aSurface->GetFormat()) == 1) {
    for (int32_t y = 0; y < aFillRect.height; y++) {
      PodCopy(data, sampleData, aFillRect.width);
      data += stride;
    }
  }
}

static void
FillRectWithHorizontallyRepeatingVerticalStrip(DataSourceSurface *aSurface,
                                               const IntRect &aFillRect,
                                               const IntRect &aSampleRect)
{
  uint8_t* data = aSurface->GetData();
  int32_t stride = aSurface->Stride();
  uint8_t* sampleData = data + DataOffset(aSurface, aSampleRect.TopLeft());
  data += DataOffset(aSurface, aFillRect.TopLeft());
  if (BytesPerPixel(aSurface->GetFormat()) == 4) {
    for (int32_t y = 0; y < aFillRect.height; y++) {
      int32_t sampleColor = *((uint32_t*)sampleData);
      for (int32_t x = 0; x < aFillRect.width; x++) {
        *((uint32_t*)data + x) = sampleColor;
      }
      data += stride;
      sampleData += stride;
    }
  } else if (BytesPerPixel(aSurface->GetFormat()) == 1) {
    for (int32_t y = 0; y < aFillRect.height; y++) {
      uint8_t sampleColor = *sampleData;
      memset(data, sampleColor, aFillRect.width);
      data += stride;
      sampleData += stride;
    }
  }
}

static void
DuplicateEdges(DataSourceSurface* aSurface, const IntRect &aFromRect)
{
  IntSize size = aSurface->GetSize();
  IntRect fill;
  IntRect sampleRect;
  for (int32_t ix = 0; ix < 3; ix++) {
    switch (ix) {
      case 0:
        fill.x = 0;
        fill.width = aFromRect.x;
        sampleRect.x = fill.XMost();
        sampleRect.width = 1;
        break;
      case 1:
        fill.x = aFromRect.x;
        fill.width = aFromRect.width;
        sampleRect.x = fill.x;
        sampleRect.width = fill.width;
        break;
      case 2:
        fill.x = aFromRect.XMost();
        fill.width = size.width - fill.x;
        sampleRect.x = fill.x - 1;
        sampleRect.width = 1;
        break;
    }
    if (fill.width <= 0) {
      continue;
    }
    bool xIsMiddle = (ix == 1);
    for (int32_t iy = 0; iy < 3; iy++) {
      switch (iy) {
        case 0:
          fill.y = 0;
          fill.height = aFromRect.y;
          sampleRect.y = fill.YMost();
          sampleRect.height = 1;
          break;
        case 1:
          fill.y = aFromRect.y;
          fill.height = aFromRect.height;
          sampleRect.y = fill.y;
          sampleRect.height = fill.height;
          break;
        case 2:
          fill.y = aFromRect.YMost();
          fill.height = size.height - fill.y;
          sampleRect.y = fill.y - 1;
          sampleRect.height = 1;
          break;
      }
      if (fill.height <= 0) {
        continue;
      }
      bool yIsMiddle = (iy == 1);
      if (!xIsMiddle && !yIsMiddle) {
        // Corner
        FillRectWithPixel(aSurface, fill, sampleRect.TopLeft());
      }
      if (xIsMiddle && !yIsMiddle) {
        // Top middle or bottom middle
        FillRectWithVerticallyRepeatingHorizontalStrip(aSurface, fill, sampleRect);
      }
      if (!xIsMiddle && yIsMiddle) {
        // Left middle or right middle
        FillRectWithHorizontallyRepeatingVerticalStrip(aSurface, fill, sampleRect);
      }
    }
  }
}

static IntPoint
TileIndex(const IntRect &aFirstTileRect, const IntPoint &aPoint)
{
  return IntPoint(int32_t(floor(double(aPoint.x - aFirstTileRect.x) / aFirstTileRect.width)),
                  int32_t(floor(double(aPoint.y - aFirstTileRect.y) / aFirstTileRect.height)));
}

static void
TileSurface(DataSourceSurface* aSource, DataSourceSurface* aTarget, const IntPoint &aOffset)
{
  IntRect sourceRect(aOffset, aSource->GetSize());
  IntRect targetRect(IntPoint(0, 0), aTarget->GetSize());
  IntPoint startIndex = TileIndex(sourceRect, targetRect.TopLeft());
  IntPoint endIndex = TileIndex(sourceRect, targetRect.BottomRight());

  for (int32_t ix = startIndex.x; ix <= endIndex.x; ix++) {
    for (int32_t iy = startIndex.y; iy <= endIndex.y; iy++) {
      IntPoint destPoint(sourceRect.x + ix * sourceRect.width,
                         sourceRect.y + iy * sourceRect.height);
      IntRect destRect(destPoint, sourceRect.Size());
      destRect = destRect.Intersect(targetRect);
      IntRect srcRect = destRect - destPoint;
      CopyRect(aSource, aTarget, srcRect, destRect.TopLeft());
    }
  }
}

static TemporaryRef<DataSourceSurface>
GetDataSurfaceInRect(SourceSurface *aSurface,
                     const IntRect &aSurfaceRect,
                     const IntRect &aDestRect,
                     ConvolveMatrixEdgeMode aEdgeMode)
{
  MOZ_ASSERT(aSurface ? aSurfaceRect.Size() == aSurface->GetSize() : aSurfaceRect.IsEmpty());
  IntRect sourceRect = aSurfaceRect;

  if (sourceRect.IsEqualEdges(aDestRect)) {
    return aSurface ? aSurface->GetDataSurface() : nullptr;
  }

  IntRect intersect = sourceRect.Intersect(aDestRect);
  IntRect intersectInSourceSpace = intersect - sourceRect.TopLeft();
  IntRect intersectInDestSpace = intersect - aDestRect.TopLeft();
  SurfaceFormat format = aSurface ? aSurface->GetFormat() : FORMAT_B8G8R8A8;

  RefPtr<DataSourceSurface> target =
    Factory::CreateDataSourceSurface(aDestRect.Size(), format);

  if (!target) {
    return nullptr;
  }

  if (aEdgeMode == EDGE_MODE_NONE && !aSurfaceRect.Contains(aDestRect)) {
    ClearDataSourceSurface(target);
  }

  if (!aSurface) {
    return target;
  }

  RefPtr<DataSourceSurface> dataSource = aSurface->GetDataSurface();
  MOZ_ASSERT(dataSource);

  if (aEdgeMode == EDGE_MODE_WRAP) {
    TileSurface(dataSource, target, intersectInDestSpace.TopLeft());
    return target;
  }

  CopyRect(dataSource, target, intersectInSourceSpace,
           intersectInDestSpace.TopLeft());

  if (aEdgeMode == EDGE_MODE_DUPLICATE) {
    DuplicateEdges(target, intersectInDestSpace);
  }

  return target;
}

/* static */ TemporaryRef<FilterNode>
FilterNodeSoftware::Create(FilterType aType)
{
  RefPtr<FilterNodeSoftware> filter;
  switch (aType) {
    case FILTER_BLEND:
      filter = new FilterNodeBlendSoftware();
      break;
    case FILTER_TRANSFORM:
      filter = new FilterNodeTransformSoftware();
      break;
    case FILTER_MORPHOLOGY:
      filter = new FilterNodeMorphologySoftware();
      break;
    case FILTER_COLOR_MATRIX:
      filter = new FilterNodeColorMatrixSoftware();
      break;
    case FILTER_FLOOD:
      filter = new FilterNodeFloodSoftware();
      break;
    case FILTER_TILE:
      filter = new FilterNodeTileSoftware();
      break;
    case FILTER_TABLE_TRANSFER:
      filter = new FilterNodeTableTransferSoftware();
      break;
    case FILTER_DISCRETE_TRANSFER:
      filter = new FilterNodeDiscreteTransferSoftware();
      break;
    case FILTER_LINEAR_TRANSFER:
      filter = new FilterNodeLinearTransferSoftware();
      break;
    case FILTER_GAMMA_TRANSFER:
      filter = new FilterNodeGammaTransferSoftware();
      break;
    case FILTER_CONVOLVE_MATRIX:
      filter = new FilterNodeConvolveMatrixSoftware();
      break;
    case FILTER_DISPLACEMENT_MAP:
      filter = new FilterNodeDisplacementMapSoftware();
      break;
    case FILTER_TURBULENCE:
      filter = new FilterNodeTurbulenceSoftware();
      break;
    case FILTER_ARITHMETIC_COMBINE:
      filter = new FilterNodeArithmeticCombineSoftware();
      break;
    case FILTER_COMPOSITE:
      filter = new FilterNodeCompositeSoftware();
      break;
    case FILTER_GAUSSIAN_BLUR:
      filter = new FilterNodeGaussianBlurSoftware();
      break;
    case FILTER_DIRECTIONAL_BLUR:
      filter = new FilterNodeDirectionalBlurSoftware();
      break;
    case FILTER_CROP:
      filter = new FilterNodeCropSoftware();
      break;
    case FILTER_PREMULTIPLY:
      filter = new FilterNodePremultiplySoftware();
      break;
    case FILTER_UNPREMULTIPLY:
      filter = new FilterNodeUnpremultiplySoftware();
      break;
    case FILTER_POINT_DIFFUSE:
      filter = new FilterNodeLightingSoftware<PointLightSoftware, DiffuseLightingSoftware>();
      break;
    case FILTER_POINT_SPECULAR:
      filter = new FilterNodeLightingSoftware<PointLightSoftware, SpecularLightingSoftware>();
      break;
    case FILTER_SPOT_DIFFUSE:
      filter = new FilterNodeLightingSoftware<SpotLightSoftware, DiffuseLightingSoftware>();
      break;
    case FILTER_SPOT_SPECULAR:
      filter = new FilterNodeLightingSoftware<SpotLightSoftware, SpecularLightingSoftware>();
      break;
    case FILTER_DISTANT_DIFFUSE:
      filter = new FilterNodeLightingSoftware<DistantLightSoftware, DiffuseLightingSoftware>();
      break;
    case FILTER_DISTANT_SPECULAR:
      filter = new FilterNodeLightingSoftware<DistantLightSoftware, SpecularLightingSoftware>();
      break;
  }
  return filter;
}

void
FilterNodeSoftware::Draw(DrawTarget* aDrawTarget,
                         const Rect &aSourceRect,
                         const Point &aDestPoint,
                         const DrawOptions &aOptions)
{
#ifdef DEBUG_DUMP_SURFACES
  printf("<pre>\nRendering...\n");
#endif

  Rect renderRect = aSourceRect;
  renderRect.RoundOut();
  IntRect renderIntRect(int32_t(renderRect.x), int32_t(renderRect.y),
                        int32_t(renderRect.width), int32_t(renderRect.height));
  IntRect outputRect = renderIntRect.Intersect(GetOutputRectInRect(renderIntRect));

  RefPtr<DataSourceSurface> result;
  if (!outputRect.IsEmpty()) {
    result = GetOutput(outputRect);
  }

  if (!result) {
    // Null results are allowed and treated as transparent. Don't draw anything.
#ifdef DEBUG_DUMP_SURFACES
    printf("output returned null\n");
    printf("</pre>\n");
#endif
    return;
  }

#ifdef DEBUG_DUMP_SURFACES
  printf("output:\n");
  printf("<img src='"); DumpAsPNG(result); printf("'>\n");
  printf("</pre>\n");
#endif

  Point sourceToDestOffset = aDestPoint - aSourceRect.TopLeft();
  Rect renderedSourceRect = Rect(outputRect).Intersect(aSourceRect);
  Rect renderedDestRect = renderedSourceRect + sourceToDestOffset;
  aDrawTarget->DrawSurface(result, renderedDestRect,
                           renderedSourceRect - Point(outputRect.TopLeft()),
                           DrawSurfaceOptions(), aOptions);
}

TemporaryRef<DataSourceSurface>
FilterNodeSoftware::GetOutput(const IntRect &aRect)
{
  MOZ_ASSERT(GetOutputRectInRect(aRect).Contains(aRect));
  if (!mCachedRect.Contains(aRect)) {
    RequestRect(aRect);
    mCachedOutput = Render(mRequestedRect);
    if (!mCachedOutput) {
      mCachedRect = IntRect();
      mRequestedRect = IntRect();
      return nullptr;
    }
    mCachedRect = mRequestedRect;
    mRequestedRect = IntRect();
  } else {
    MOZ_ASSERT(mCachedOutput, "cached rect but no cached output?");
  }
  return GetDataSurfaceInRect(mCachedOutput, mCachedRect, aRect, EDGE_MODE_NONE);
}

void
FilterNodeSoftware::RequestRect(const IntRect &aRect)
{
  mRequestedRect = mRequestedRect.Union(aRect);
  RequestFromInputsForRect(aRect);
}

void
FilterNodeSoftware::RequestInputRect(uint32_t aInputEnumIndex, const IntRect &aRect)
{
  int32_t inputIndex = InputIndex(aInputEnumIndex);
  if (inputIndex < 0 || (uint32_t)inputIndex >= NumberOfSetInputs()) {
    MOZ_CRASH();
  }
  if (mInputSurfaces[inputIndex]) {
    return;
  }
  RefPtr<FilterNodeSoftware> filter = mInputFilters[inputIndex];
  MOZ_ASSERT(filter, "missing input");
  filter->RequestRect(filter->GetOutputRectInRect(aRect));
}

SurfaceFormat
FilterNodeSoftware::DesiredFormat(SurfaceFormat aCurrentFormat,
                                  FormatHint aFormatHint)
{
  if (aCurrentFormat == FORMAT_A8 && aFormatHint == CAN_HANDLE_A8) {
    return FORMAT_A8;
  }
  return FORMAT_B8G8R8A8;
}

TemporaryRef<DataSourceSurface>
FilterNodeSoftware::GetInputDataSourceSurface(uint32_t aInputEnumIndex,
                                              const IntRect& aRect,
                                              FormatHint aFormatHint,
                                              ConvolveMatrixEdgeMode aEdgeMode,
                                              const IntRect *aTransparencyPaddedSourceRect)
{
#ifdef DEBUG_DUMP_SURFACES
  printf("<h1>GetInputDataSourceSurface with aRect: %d, %d, %d, %d</h1>\n",
         aRect.x, aRect.y, aRect.width, aRect.height);
#endif
  int32_t inputIndex = InputIndex(aInputEnumIndex);
  if (inputIndex < 0 || (uint32_t)inputIndex >= NumberOfSetInputs()) {
    MOZ_CRASH();
    return nullptr;
  }

  if (aRect.IsEmpty()) {
    return nullptr;
  }

  RefPtr<SourceSurface> surface;
  IntRect surfaceRect;
  if (mInputSurfaces[inputIndex]) {
    surface = mInputSurfaces[inputIndex];
#ifdef DEBUG_DUMP_SURFACES
    printf("input from input surface:\n");
    printf("<img src='"); DumpAsPNG(surface); printf("'>\n");
#endif
    surfaceRect = IntRect(IntPoint(0, 0), surface->GetSize());
  } else {
    RefPtr<FilterNodeSoftware> filter = mInputFilters[inputIndex];
    MOZ_ASSERT(filter, "missing input");
    IntRect inputFilterOutput = filter->GetOutputRectInRect(aRect);
    if (!inputFilterOutput.IsEmpty()) {
      surface = filter->GetOutput(inputFilterOutput);
    }
    surfaceRect = inputFilterOutput;
    MOZ_ASSERT(!surface || surfaceRect.Size() == surface->GetSize());
  }

  if (surface && surface->GetFormat() == FORMAT_UNKNOWN) {
#ifdef DEBUG_DUMP_SURFACES
    printf("wrong input format\n\n");
#endif
    return nullptr;
  }

  if (!surfaceRect.IsEmpty() && !surface) {
#ifdef DEBUG_DUMP_SURFACES
    printf(" -- no input --\n\n");
#endif
    return nullptr;
  }

  if (aTransparencyPaddedSourceRect && !aTransparencyPaddedSourceRect->IsEmpty()) {
    IntRect srcRect = aTransparencyPaddedSourceRect->Intersect(aRect);
    surface = GetDataSurfaceInRect(surface, surfaceRect, srcRect, EDGE_MODE_NONE);
    surfaceRect = srcRect;
  }

  RefPtr<DataSourceSurface> result =
    GetDataSurfaceInRect(surface, surfaceRect, aRect, aEdgeMode);

  if (!result) {
#ifdef DEBUG_DUMP_SURFACES
    printf(" -- no input --\n\n");
#endif
    return nullptr;
  }

  if (result->Stride() != GetAlignedStride<16>(result->Stride()) ||
      reinterpret_cast<uintptr_t>(result->GetData()) % 16 != 0) {
    // Align unaligned surface.
    result = CloneAligned(result);
  }

  SurfaceFormat currentFormat = result->GetFormat();
  if (DesiredFormat(currentFormat, aFormatHint) == FORMAT_B8G8R8A8 &&
      currentFormat != FORMAT_B8G8R8A8) {
    result = FilterProcessing::ConvertToB8G8R8A8(result);
  }

#ifdef DEBUG_DUMP_SURFACES
  printf("input:\n");
  printf("<img src='"); DumpAsPNG(result); printf("'>\n");
#endif

  MOZ_ASSERT(!result || result->GetSize() == aRect.Size(), "wrong surface size");

  return result;
}

IntRect
FilterNodeSoftware::GetInputRectInRect(uint32_t aInputEnumIndex,
                                       const IntRect &aInRect)
{
  int32_t inputIndex = InputIndex(aInputEnumIndex);
  if (inputIndex < 0 || (uint32_t)inputIndex >= NumberOfSetInputs()) {
    MOZ_CRASH();
    return IntRect();
  }
  if (mInputSurfaces[inputIndex]) {
    return aInRect.Intersect(IntRect(IntPoint(0, 0),
                                     mInputSurfaces[inputIndex]->GetSize()));
  }
  RefPtr<FilterNodeSoftware> filter = mInputFilters[inputIndex];
  MOZ_ASSERT(filter, "missing input");
  return filter->GetOutputRectInRect(aInRect);
}

size_t
FilterNodeSoftware::NumberOfSetInputs()
{
  return std::max(mInputSurfaces.size(), mInputFilters.size());
}

void
FilterNodeSoftware::AddInvalidationListener(FilterInvalidationListener* aListener)
{
  MOZ_ASSERT(aListener, "null listener");
  mInvalidationListeners.push_back(aListener);
}

void
FilterNodeSoftware::RemoveInvalidationListener(FilterInvalidationListener* aListener)
{
  MOZ_ASSERT(aListener, "null listener");
  std::vector<FilterInvalidationListener*>::iterator it =
    std::find(mInvalidationListeners.begin(), mInvalidationListeners.end(), aListener);
  mInvalidationListeners.erase(it);
}

void
FilterNodeSoftware::FilterInvalidated(FilterNodeSoftware* aFilter)
{
  Invalidate();
}

void
FilterNodeSoftware::Invalidate()
{
  mCachedOutput = nullptr;
  mCachedRect = IntRect();
  for (std::vector<FilterInvalidationListener*>::iterator it = mInvalidationListeners.begin();
       it != mInvalidationListeners.end(); it++) {
    (*it)->FilterInvalidated(this);
  }
}

FilterNodeSoftware::~FilterNodeSoftware()
{
  MOZ_ASSERT(!mInvalidationListeners.size(),
             "All invalidation listeners should have unsubscribed themselves by now!");

  for (std::vector<RefPtr<FilterNodeSoftware> >::iterator it = mInputFilters.begin();
       it != mInputFilters.end(); it++) {
    if (*it) {
      (*it)->RemoveInvalidationListener(this);
    }
  }
}

void
FilterNodeSoftware::SetInput(uint32_t aIndex, FilterNode *aFilter)
{
  if (aFilter->GetBackendType() != FILTER_BACKEND_SOFTWARE) {
    MOZ_ASSERT(false, "can only take software filters as inputs");
    return;
  }
  SetInput(aIndex, nullptr, static_cast<FilterNodeSoftware*>(aFilter));
}

void
FilterNodeSoftware::SetInput(uint32_t aIndex, SourceSurface *aSurface)
{
  SetInput(aIndex, aSurface, nullptr);
}

void
FilterNodeSoftware::SetInput(uint32_t aInputEnumIndex,
                             SourceSurface *aSurface,
                             FilterNodeSoftware *aFilter)
{
  int32_t inputIndex = InputIndex(aInputEnumIndex);
  if (inputIndex < 0) {
    MOZ_CRASH();
    return;
  }
  if ((uint32_t)inputIndex >= mInputSurfaces.size()) {
    mInputSurfaces.resize(inputIndex + 1);
  }
  if ((uint32_t)inputIndex >= mInputFilters.size()) {
    mInputFilters.resize(inputIndex + 1);
  }
  mInputSurfaces[inputIndex] = aSurface;
  if (mInputFilters[inputIndex]) {
    mInputFilters[inputIndex]->RemoveInvalidationListener(this);
  }
  if (aFilter) {
    aFilter->AddInvalidationListener(this);
  }
  mInputFilters[inputIndex] = aFilter;
  Invalidate();
}

FilterNodeBlendSoftware::FilterNodeBlendSoftware()
 : mBlendMode(BLEND_MODE_MULTIPLY)
{}

int32_t
FilterNodeBlendSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_BLEND_IN: return 0;
    case IN_BLEND_IN2: return 1;
    default: return -1;
  }
}

void
FilterNodeBlendSoftware::SetAttribute(uint32_t aIndex, uint32_t aBlendMode)
{
  MOZ_ASSERT(aIndex == ATT_BLEND_BLENDMODE);
  mBlendMode = static_cast<BlendMode>(aBlendMode);
  Invalidate();
}

TemporaryRef<DataSourceSurface>
FilterNodeBlendSoftware::Render(const IntRect& aRect)
{
  RefPtr<DataSourceSurface> input1 =
    GetInputDataSourceSurface(IN_BLEND_IN, aRect, NEED_COLOR_CHANNELS);
  RefPtr<DataSourceSurface> input2 =
    GetInputDataSourceSurface(IN_BLEND_IN2, aRect, NEED_COLOR_CHANNELS);

  // Null inputs need to be treated as transparent.

  // First case: both are transparent.
  if (!input1 && !input2) {
    // Then the result is transparent, too.
    return nullptr;
  }

  // Second case: both are non-transparent.
  if (input1 && input2) {
    // Apply normal filtering.
    return FilterProcessing::ApplyBlending(input1, input2, mBlendMode);
  }

  // Third case: one of them is transparent. Return the non-transparent one.
  return input1 ? input1 : input2;
}

void
FilterNodeBlendSoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  RequestInputRect(IN_BLEND_IN, aRect);
  RequestInputRect(IN_BLEND_IN2, aRect);
}

IntRect
FilterNodeBlendSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  return GetInputRectInRect(IN_BLEND_IN, aRect).Union(
    GetInputRectInRect(IN_BLEND_IN2, aRect)).Intersect(aRect);
}

FilterNodeTransformSoftware::FilterNodeTransformSoftware()
 : mFilter(FILTER_GOOD)
{}

int32_t
FilterNodeTransformSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_TRANSFORM_IN: return 0;
    default: return -1;
  }
}

void
FilterNodeTransformSoftware::SetAttribute(uint32_t aIndex, uint32_t aFilter)
{
  MOZ_ASSERT(aIndex == ATT_TRANSFORM_FILTER);
  mFilter = static_cast<Filter>(aFilter);
  Invalidate();
}

void
FilterNodeTransformSoftware::SetAttribute(uint32_t aIndex, const Matrix &aMatrix)
{
  MOZ_ASSERT(aIndex == ATT_TRANSFORM_MATRIX);
  mMatrix = aMatrix;
  Invalidate();
}

IntRect
FilterNodeTransformSoftware::SourceRectForOutputRect(const IntRect &aRect)
{
  if (aRect.IsEmpty()) {
    return IntRect();
  }

  Matrix inverted(mMatrix);
  if (!inverted.Invert()) {
    return IntRect();
  }

  Rect neededRect = inverted.TransformBounds(Rect(aRect));
  neededRect.RoundOut();
  return GetInputRectInRect(IN_TRANSFORM_IN, RoundedToInt(neededRect));
}

TemporaryRef<DataSourceSurface>
FilterNodeTransformSoftware::Render(const IntRect& aRect)
{
  IntRect srcRect = SourceRectForOutputRect(aRect);

  RefPtr<DataSourceSurface> input =
    GetInputDataSourceSurface(IN_TRANSFORM_IN, srcRect, NEED_COLOR_CHANNELS);

  if (!input) {
    return nullptr;
  }

  Matrix transform = Matrix().Translate(srcRect.x, srcRect.y) * mMatrix *
                     Matrix().Translate(-aRect.x, -aRect.y);
  if (transform.IsIdentity() && srcRect.Size() == aRect.Size()) {
    return input;
  }

  RefPtr<DrawTarget> dt =
    Factory::CreateDrawTarget(BACKEND_CAIRO, aRect.Size(), input->GetFormat());
  if (!dt) {
    return nullptr;
  }

  Rect r(0, 0, srcRect.width, srcRect.height);
  dt->SetTransform(transform);
  dt->DrawSurface(input, r, r, DrawSurfaceOptions(mFilter));

  RefPtr<SourceSurface> result = dt->Snapshot();
  RefPtr<DataSourceSurface> resultData = result->GetDataSurface();
  return resultData;
}

void
FilterNodeTransformSoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  RequestInputRect(IN_TRANSFORM_IN, SourceRectForOutputRect(aRect));
}

IntRect
FilterNodeTransformSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  IntRect srcRect = SourceRectForOutputRect(aRect);
  if (srcRect.IsEmpty()) {
    return IntRect();
  }

  Rect outRect = mMatrix.TransformBounds(Rect(srcRect));
  outRect.RoundOut();
  return RoundedToInt(outRect).Intersect(aRect);
}

FilterNodeMorphologySoftware::FilterNodeMorphologySoftware()
 : mOperator(MORPHOLOGY_OPERATOR_ERODE)
{}

int32_t
FilterNodeMorphologySoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_MORPHOLOGY_IN: return 0;
    default: return -1;
  }
}

void
FilterNodeMorphologySoftware::SetAttribute(uint32_t aIndex,
                                           const IntSize &aRadii)
{
  MOZ_ASSERT(aIndex == ATT_MORPHOLOGY_RADII);
  mRadii.width = std::min(std::max(aRadii.width, 0), 100000);
  mRadii.height = std::min(std::max(aRadii.height, 0), 100000);
  Invalidate();
}

void
FilterNodeMorphologySoftware::SetAttribute(uint32_t aIndex,
                                           uint32_t aOperator)
{
  MOZ_ASSERT(aIndex == ATT_MORPHOLOGY_OPERATOR);
  mOperator = static_cast<MorphologyOperator>(aOperator);
  Invalidate();
}

static TemporaryRef<DataSourceSurface>
ApplyMorphology(const IntRect& aSourceRect, DataSourceSurface* aInput,
                const IntRect& aDestRect, int32_t rx, int32_t ry,
                MorphologyOperator aOperator)
{
  IntRect srcRect = aSourceRect - aDestRect.TopLeft();
  IntRect destRect = aDestRect - aDestRect.TopLeft();
  IntRect tmpRect(destRect.x, srcRect.y, destRect.width, srcRect.height);
#ifdef DEBUG
  IntMargin margin = srcRect - destRect;
  MOZ_ASSERT(margin.top >= ry && margin.right >= rx &&
             margin.bottom >= ry && margin.left >= rx, "insufficient margin");
#endif

  RefPtr<DataSourceSurface> tmp;
  if (rx == 0) {
    tmp = aInput;
  } else {
    tmp = Factory::CreateDataSourceSurface(tmpRect.Size(), FORMAT_B8G8R8A8);
    if (!tmp) {
      return nullptr;
    }

    int32_t sourceStride = aInput->Stride();
    uint8_t* sourceData = aInput->GetData();
    sourceData += DataOffset(aInput, destRect.TopLeft() - srcRect.TopLeft());

    int32_t tmpStride = tmp->Stride();
    uint8_t* tmpData = tmp->GetData();
    tmpData += DataOffset(tmp, destRect.TopLeft() - tmpRect.TopLeft());

    FilterProcessing::ApplyMorphologyHorizontal(
      sourceData, sourceStride, tmpData, tmpStride, tmpRect, rx, aOperator);
  }

  RefPtr<DataSourceSurface> dest;
  if (ry == 0) {
    dest = tmp;
  } else {
    dest = Factory::CreateDataSourceSurface(destRect.Size(), FORMAT_B8G8R8A8);
    if (!dest) {
      return nullptr;
    }

    int32_t tmpStride = tmp->Stride();
    uint8_t* tmpData = tmp->GetData();
    tmpData += DataOffset(tmp, destRect.TopLeft() - tmpRect.TopLeft());

    int32_t destStride = dest->Stride();
    uint8_t* destData = dest->GetData();

    FilterProcessing::ApplyMorphologyVertical(
      tmpData, tmpStride, destData, destStride, destRect, ry, aOperator);
  }

  return dest;
}

TemporaryRef<DataSourceSurface>
FilterNodeMorphologySoftware::Render(const IntRect& aRect)
{
  IntRect srcRect = aRect;
  srcRect.Inflate(mRadii);

  RefPtr<DataSourceSurface> input =
    GetInputDataSourceSurface(IN_MORPHOLOGY_IN, srcRect, NEED_COLOR_CHANNELS);
  if (!input) {
    return nullptr;
  }

  int32_t rx = mRadii.width;
  int32_t ry = mRadii.height;

  if (rx == 0 && ry == 0) {
    return input;
  }

  return ApplyMorphology(srcRect, input, aRect, rx, ry, mOperator);
}

void
FilterNodeMorphologySoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  IntRect srcRect = aRect;
  srcRect.Inflate(mRadii);
  RequestInputRect(IN_MORPHOLOGY_IN, srcRect);
}

IntRect
FilterNodeMorphologySoftware::GetOutputRectInRect(const IntRect& aRect)
{
  IntRect inflatedSourceRect = aRect;
  inflatedSourceRect.Inflate(mRadii);
  IntRect inputRect = GetInputRectInRect(IN_MORPHOLOGY_IN, inflatedSourceRect);
  if (mOperator == MORPHOLOGY_OPERATOR_ERODE) {
    inputRect.Deflate(mRadii);
  } else {
    inputRect.Inflate(mRadii);
  }
  return inputRect.Intersect(aRect);
}

int32_t
FilterNodeColorMatrixSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_COLOR_MATRIX_IN: return 0;
    default: return -1;
  }
}

void
FilterNodeColorMatrixSoftware::SetAttribute(uint32_t aIndex,
                                            const Matrix5x4 &aMatrix)
{
  MOZ_ASSERT(aIndex == ATT_COLOR_MATRIX_MATRIX);
  mMatrix = aMatrix;
  Invalidate();
}

void
FilterNodeColorMatrixSoftware::SetAttribute(uint32_t aIndex,
                                            uint32_t aAlphaMode)
{
  MOZ_ASSERT(aIndex == ATT_COLOR_MATRIX_ALPHA_MODE);
  mAlphaMode = (AlphaMode)aAlphaMode;
  Invalidate();
}

static TemporaryRef<DataSourceSurface>
Premultiply(DataSourceSurface* aSurface)
{
  if (aSurface->GetFormat() == FORMAT_A8) {
    return aSurface;
  }

  IntSize size = aSurface->GetSize();
  RefPtr<DataSourceSurface> target =
    Factory::CreateDataSourceSurface(size, FORMAT_B8G8R8A8);
  if (!target) {
    return nullptr;
  }

  uint8_t* inputData = aSurface->GetData();
  int32_t inputStride = aSurface->Stride();
  uint8_t* targetData = target->GetData();
  int32_t targetStride = target->Stride();

  FilterProcessing::DoPremultiplicationCalculation(
    size, targetData, targetStride, inputData, inputStride);

  return target;
}

static TemporaryRef<DataSourceSurface>
Unpremultiply(DataSourceSurface* aSurface)
{
  if (aSurface->GetFormat() == FORMAT_A8) {
    return aSurface;
  }

  IntSize size = aSurface->GetSize();
  RefPtr<DataSourceSurface> target =
    Factory::CreateDataSourceSurface(size, FORMAT_B8G8R8A8);
  if (!target) {
    return nullptr;
  }

  uint8_t* inputData = aSurface->GetData();
  int32_t inputStride = aSurface->Stride();
  uint8_t* targetData = target->GetData();
  int32_t targetStride = target->Stride();

  FilterProcessing::DoUnpremultiplicationCalculation(
    size, targetData, targetStride, inputData, inputStride);

  return target;
}

TemporaryRef<DataSourceSurface>
FilterNodeColorMatrixSoftware::Render(const IntRect& aRect)
{
  RefPtr<DataSourceSurface> input =
    GetInputDataSourceSurface(IN_COLOR_MATRIX_IN, aRect, NEED_COLOR_CHANNELS);
  if (!input) {
    return nullptr;
  }

  if (mAlphaMode == ALPHA_MODE_PREMULTIPLIED) {
    input = Unpremultiply(input);
  }

  RefPtr<DataSourceSurface> result =
    FilterProcessing::ApplyColorMatrix(input, mMatrix);

  if (mAlphaMode == ALPHA_MODE_PREMULTIPLIED) {
    result = Premultiply(result);
  }

  return result;
}

void
FilterNodeColorMatrixSoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  RequestInputRect(IN_COLOR_MATRIX_IN, aRect);
}

IntRect
FilterNodeColorMatrixSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  return GetInputRectInRect(IN_COLOR_MATRIX_IN, aRect);
}

void
FilterNodeFloodSoftware::SetAttribute(uint32_t aIndex, const Color &aColor)
{
  MOZ_ASSERT(aIndex == ATT_FLOOD_COLOR);
  mColor = aColor;
  Invalidate();
}

static uint32_t
ColorToBGRA(const Color& aColor)
{
  union {
    uint32_t color;
    uint8_t components[4];
  };
  components[B8G8R8A8_COMPONENT_BYTEOFFSET_R] = NS_lround(aColor.r * aColor.a * 255.0f);
  components[B8G8R8A8_COMPONENT_BYTEOFFSET_G] = NS_lround(aColor.g * aColor.a * 255.0f);
  components[B8G8R8A8_COMPONENT_BYTEOFFSET_B] = NS_lround(aColor.b * aColor.a * 255.0f);
  components[B8G8R8A8_COMPONENT_BYTEOFFSET_A] = NS_lround(aColor.a * 255.0f);
  return color;
}

static SurfaceFormat
FormatForColor(Color aColor)
{
  if (aColor.r == 0 && aColor.g == 0 && aColor.b == 0) {
    return FORMAT_A8;
  }
  return FORMAT_B8G8R8A8;
}

TemporaryRef<DataSourceSurface>
FilterNodeFloodSoftware::Render(const IntRect& aRect)
{
  SurfaceFormat format = FormatForColor(mColor);
  RefPtr<DataSourceSurface> target =
    Factory::CreateDataSourceSurface(aRect.Size(), format);
  if (!target) {
    return nullptr;
  }

  uint8_t* targetData = target->GetData();
  uint32_t stride = target->Stride();

  if (format == FORMAT_B8G8R8A8) {
    uint32_t color = ColorToBGRA(mColor);
    for (int32_t y = 0; y < aRect.height; y++) {
      for (int32_t x = 0; x < aRect.width; x++) {
        *((uint32_t*)targetData + x) = color;
      }
      targetData += stride;
    }
  } else if (format == FORMAT_A8) {
    uint8_t alpha = NS_lround(mColor.a * 255.0f);
    for (int32_t y = 0; y < aRect.height; y++) {
      for (int32_t x = 0; x < aRect.width; x++) {
        targetData[x] = alpha;
      }
      targetData += stride;
    }
  } else {
    MOZ_CRASH();
  }

  return target;
}

// Override GetOutput to get around caching. Rendering simple floods is
// comparatively fast.
TemporaryRef<DataSourceSurface>
FilterNodeFloodSoftware::GetOutput(const IntRect& aRect)
{
  return Render(aRect);
}

IntRect
FilterNodeFloodSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  return aRect;
}

int32_t
FilterNodeTileSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_TILE_IN: return 0;
    default: return -1;
  }
}

void
FilterNodeTileSoftware::SetAttribute(uint32_t aIndex,
                                     const IntRect &aSourceRect)
{
  MOZ_ASSERT(aIndex == ATT_TILE_SOURCE_RECT);
  mSourceRect = IntRect(int32_t(aSourceRect.x), int32_t(aSourceRect.y),
                        int32_t(aSourceRect.width), int32_t(aSourceRect.height));
  Invalidate();
}

namespace {
struct CompareIntRects
{
  bool operator()(const IntRect& a, const IntRect& b) const
  {
    if (a.x != b.x) {
      return a.x < b.x;
    }
    if (a.y != b.y) {
      return a.y < b.y;
    }
    if (a.width != b.width) {
      return a.width < b.width;
    }
    return a.height < b.height;
  }
};
}

TemporaryRef<DataSourceSurface>
FilterNodeTileSoftware::Render(const IntRect& aRect)
{
  if (mSourceRect.IsEmpty()) {
    return nullptr;
  }

  if (mSourceRect.Contains(aRect)) {
    return GetInputDataSourceSurface(IN_TILE_IN, aRect);
  }

  RefPtr<DataSourceSurface> target;

  typedef std::map<IntRect, RefPtr<DataSourceSurface>, CompareIntRects> InputMap;
  InputMap inputs;

  IntPoint startIndex = TileIndex(mSourceRect, aRect.TopLeft());
  IntPoint endIndex = TileIndex(mSourceRect, aRect.BottomRight());
  for (int32_t ix = startIndex.x; ix <= endIndex.x; ix++) {
    for (int32_t iy = startIndex.y; iy <= endIndex.y; iy++) {
      IntPoint sourceToDestOffset(ix * mSourceRect.width,
                                  iy * mSourceRect.height);
      IntRect destRect = aRect.Intersect(mSourceRect + sourceToDestOffset);
      IntRect srcRect = destRect - sourceToDestOffset;
      if (srcRect.IsEmpty()) {
        continue;
      }

      RefPtr<DataSourceSurface> input;
      InputMap::iterator it = inputs.find(srcRect);
      if (it == inputs.end()) {
        input = GetInputDataSourceSurface(IN_TILE_IN, srcRect);
        inputs[srcRect] = input;
      } else {
        input = it->second;
      }
      if (!input) {
        return nullptr;
      }
      if (!target) {
        // We delay creating the target until now because we want to use the
        // same format as our input filter, and we do not actually know the
        // input format before we call GetInputDataSourceSurface.
        target = Factory::CreateDataSourceSurface(aRect.Size(), input->GetFormat());
        if (!target) {
          return nullptr;
        }
      }
      MOZ_ASSERT(input->GetFormat() == target->GetFormat(), "different surface formats from the same input?");

      CopyRect(input, target, srcRect - srcRect.TopLeft(), destRect.TopLeft() - aRect.TopLeft());
    }
  }

  return target;
}

void
FilterNodeTileSoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  // Do not request anything.
  // Source rects for the tile filter can be discontinuous with large gaps
  // between them. Requesting those from our input filter might cause it to
  // render the whole bounding box of all of them, which would be wasteful.
}

IntRect
FilterNodeTileSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  return aRect;
}

FilterNodeComponentTransferSoftware::FilterNodeComponentTransferSoftware()
 : mDisableR(true)
 , mDisableG(true)
 , mDisableB(true)
 , mDisableA(true)
{}

void
FilterNodeComponentTransferSoftware::SetAttribute(uint32_t aIndex,
                                                  bool aDisable)
{
  switch (aIndex) {
    case ATT_TRANSFER_DISABLE_R:
      mDisableR = aDisable;
      break;
    case ATT_TRANSFER_DISABLE_G:
      mDisableG = aDisable;
      break;
    case ATT_TRANSFER_DISABLE_B:
      mDisableB = aDisable;
      break;
    case ATT_TRANSFER_DISABLE_A:
      mDisableA = aDisable;
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

void
FilterNodeComponentTransferSoftware::GenerateLookupTable(ptrdiff_t aComponent,
                                                         uint8_t aTables[4][256],
                                                         bool aDisabled)
{
  if (aDisabled) {
    static uint8_t sIdentityLookupTable[256];
    static bool sInitializedIdentityLookupTable = false;
    if (!sInitializedIdentityLookupTable) {
      for (int32_t i = 0; i < 256; i++) {
        sIdentityLookupTable[i] = i;
      }
      sInitializedIdentityLookupTable = true;
    }
    memcpy(aTables[aComponent], sIdentityLookupTable, 256);
  } else {
    FillLookupTable(aComponent, aTables[aComponent]);
  }
}

template<uint32_t BytesPerPixel>
static void TransferComponents(DataSourceSurface* aInput,
                               DataSourceSurface* aTarget,
                               const uint8_t aLookupTables[BytesPerPixel][256])
{
  MOZ_ASSERT(aInput->GetFormat() == aTarget->GetFormat(), "different formats");
  IntSize size = aInput->GetSize();

  uint8_t* sourceData = aInput->GetData();
  uint8_t* targetData = aTarget->GetData();
  uint32_t sourceStride = aInput->Stride();
  uint32_t targetStride = aTarget->Stride();

  for (int32_t y = 0; y < size.height; y++) {
    for (int32_t x = 0; x < size.width; x++) {
      uint32_t sourceIndex = y * sourceStride + x * BytesPerPixel;
      uint32_t targetIndex = y * targetStride + x * BytesPerPixel;
      for (uint32_t i = 0; i < BytesPerPixel; i++) {
        targetData[targetIndex + i] = aLookupTables[i][sourceData[sourceIndex + i]];
      }
    }
  }
}

bool
IsAllZero(uint8_t aLookupTable[256])
{
  for (int32_t i = 0; i < 256; i++) {
    if (aLookupTable[i] != 0) {
      return false;
    }
  }
  return true;
}

TemporaryRef<DataSourceSurface>
FilterNodeComponentTransferSoftware::Render(const IntRect& aRect)
{
  if (mDisableR && mDisableG && mDisableB && mDisableA) {
    return GetInputDataSourceSurface(IN_TRANSFER_IN, aRect);
  }

  uint8_t lookupTables[4][256];
  GenerateLookupTable(B8G8R8A8_COMPONENT_BYTEOFFSET_R, lookupTables, mDisableR);
  GenerateLookupTable(B8G8R8A8_COMPONENT_BYTEOFFSET_G, lookupTables, mDisableG);
  GenerateLookupTable(B8G8R8A8_COMPONENT_BYTEOFFSET_B, lookupTables, mDisableB);
  GenerateLookupTable(B8G8R8A8_COMPONENT_BYTEOFFSET_A, lookupTables, mDisableA);

  bool needColorChannels =
    lookupTables[B8G8R8A8_COMPONENT_BYTEOFFSET_R][0] != 0 ||
    lookupTables[B8G8R8A8_COMPONENT_BYTEOFFSET_G][0] != 0 ||
    lookupTables[B8G8R8A8_COMPONENT_BYTEOFFSET_B][0] != 0;

  FormatHint pref = needColorChannels ? NEED_COLOR_CHANNELS : CAN_HANDLE_A8;

  RefPtr<DataSourceSurface> input =
    GetInputDataSourceSurface(IN_TRANSFER_IN, aRect, pref);
  if (!input) {
    return nullptr;
  }

  if (input->GetFormat() == FORMAT_B8G8R8A8 && !needColorChannels) {
    bool colorChannelsBecomeBlack =
      IsAllZero(lookupTables[B8G8R8A8_COMPONENT_BYTEOFFSET_R]) &&
      IsAllZero(lookupTables[B8G8R8A8_COMPONENT_BYTEOFFSET_G]) &&
      IsAllZero(lookupTables[B8G8R8A8_COMPONENT_BYTEOFFSET_B]);

    if (colorChannelsBecomeBlack) {
      input = FilterProcessing::ExtractAlpha(input);
    }
  }

  SurfaceFormat format = input->GetFormat();
  if (format == FORMAT_A8 && mDisableA) {
    return input;
  }

  RefPtr<DataSourceSurface> target =
    Factory::CreateDataSourceSurface(aRect.Size(), format);
  if (!target) {
    return nullptr;
  }

  if (format == FORMAT_A8) {
    TransferComponents<1>(input, target, &lookupTables[B8G8R8A8_COMPONENT_BYTEOFFSET_A]);
  } else {
    TransferComponents<4>(input, target, lookupTables);
  }

  return target;
}

void
FilterNodeComponentTransferSoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  RequestInputRect(IN_TRANSFER_IN, aRect);
}

IntRect
FilterNodeComponentTransferSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  return GetInputRectInRect(IN_TRANSFER_IN, aRect);
}

int32_t
FilterNodeComponentTransferSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_TRANSFER_IN: return 0;
    default: return -1;
  }
}

void
FilterNodeTableTransferSoftware::SetAttribute(uint32_t aIndex,
                                              const Float* aFloat,
                                              uint32_t aSize)
{
  std::vector<Float> table(aFloat, aFloat + aSize);
  switch (aIndex) {
    case ATT_TABLE_TRANSFER_TABLE_R:
      mTableR = table;
      break;
    case ATT_TABLE_TRANSFER_TABLE_G:
      mTableG = table;
      break;
    case ATT_TABLE_TRANSFER_TABLE_B:
      mTableB = table;
      break;
    case ATT_TABLE_TRANSFER_TABLE_A:
      mTableA = table;
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

void
FilterNodeTableTransferSoftware::FillLookupTable(ptrdiff_t aComponent,
                                                 uint8_t aTable[256])
{
  switch (aComponent) {
    case B8G8R8A8_COMPONENT_BYTEOFFSET_R:
      FillLookupTableImpl(mTableR, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_G:
      FillLookupTableImpl(mTableG, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_B:
      FillLookupTableImpl(mTableB, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_A:
      FillLookupTableImpl(mTableA, aTable);
      break;
    default:
      MOZ_ASSERT(false, "unknown component");
      break;
  }
}

void
FilterNodeTableTransferSoftware::FillLookupTableImpl(std::vector<Float>& aTableValues,
                                                     uint8_t aTable[256])
{
  uint32_t tvLength = aTableValues.size();
  if (tvLength < 2) {
    return;
  }

  for (size_t i = 0; i < 256; i++) {
    uint32_t k = (i * (tvLength - 1)) / 255;
    Float v1 = aTableValues[k];
    Float v2 = aTableValues[std::min(k + 1, tvLength - 1)];
    int32_t val =
      int32_t(255 * (v1 + (i/255.0f - k/float(tvLength-1))*(tvLength - 1)*(v2 - v1)));
    val = std::min(255, val);
    val = std::max(0, val);
    aTable[i] = val;
  }
}

void
FilterNodeDiscreteTransferSoftware::SetAttribute(uint32_t aIndex,
                                              const Float* aFloat,
                                              uint32_t aSize)
{
  std::vector<Float> discrete(aFloat, aFloat + aSize);
  switch (aIndex) {
    case ATT_DISCRETE_TRANSFER_TABLE_R:
      mTableR = discrete;
      break;
    case ATT_DISCRETE_TRANSFER_TABLE_G:
      mTableG = discrete;
      break;
    case ATT_DISCRETE_TRANSFER_TABLE_B:
      mTableB = discrete;
      break;
    case ATT_DISCRETE_TRANSFER_TABLE_A:
      mTableA = discrete;
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

void
FilterNodeDiscreteTransferSoftware::FillLookupTable(ptrdiff_t aComponent,
                                                    uint8_t aTable[256])
{
  switch (aComponent) {
    case B8G8R8A8_COMPONENT_BYTEOFFSET_R:
      FillLookupTableImpl(mTableR, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_G:
      FillLookupTableImpl(mTableG, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_B:
      FillLookupTableImpl(mTableB, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_A:
      FillLookupTableImpl(mTableA, aTable);
      break;
    default:
      MOZ_ASSERT(false, "unknown component");
      break;
  }
}

void
FilterNodeDiscreteTransferSoftware::FillLookupTableImpl(std::vector<Float>& aTableValues,
                                                        uint8_t aTable[256])
{
  uint32_t tvLength = aTableValues.size();
  if (tvLength < 1) {
    return;
  }

  for (size_t i = 0; i < 256; i++) {
    uint32_t k = (i * tvLength) / 255;
    k = std::min(k, tvLength - 1);
    Float v = aTableValues[k];
    int32_t val = NS_lround(255 * v);
    val = std::min(255, val);
    val = std::max(0, val);
    aTable[i] = val;
  }
}

FilterNodeLinearTransferSoftware::FilterNodeLinearTransferSoftware()
 : mSlopeR(0)
 , mSlopeG(0)
 , mSlopeB(0)
 , mSlopeA(0)
 , mInterceptR(0)
 , mInterceptG(0)
 , mInterceptB(0)
 , mInterceptA(0)
{}

void
FilterNodeLinearTransferSoftware::SetAttribute(uint32_t aIndex,
                                               Float aValue)
{
  switch (aIndex) {
    case ATT_LINEAR_TRANSFER_SLOPE_R:
      mSlopeR = aValue;
      break;
    case ATT_LINEAR_TRANSFER_INTERCEPT_R:
      mInterceptR = aValue;
      break;
    case ATT_LINEAR_TRANSFER_SLOPE_G:
      mSlopeG = aValue;
      break;
    case ATT_LINEAR_TRANSFER_INTERCEPT_G:
      mInterceptG = aValue;
      break;
    case ATT_LINEAR_TRANSFER_SLOPE_B:
      mSlopeB = aValue;
      break;
    case ATT_LINEAR_TRANSFER_INTERCEPT_B:
      mInterceptB = aValue;
      break;
    case ATT_LINEAR_TRANSFER_SLOPE_A:
      mSlopeA = aValue;
      break;
    case ATT_LINEAR_TRANSFER_INTERCEPT_A:
      mInterceptA = aValue;
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

void
FilterNodeLinearTransferSoftware::FillLookupTable(ptrdiff_t aComponent,
                                                  uint8_t aTable[256])
{
  switch (aComponent) {
    case B8G8R8A8_COMPONENT_BYTEOFFSET_R:
      FillLookupTableImpl(mSlopeR, mInterceptR, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_G:
      FillLookupTableImpl(mSlopeG, mInterceptG, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_B:
      FillLookupTableImpl(mSlopeB, mInterceptB, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_A:
      FillLookupTableImpl(mSlopeA, mInterceptA, aTable);
      break;
    default:
      MOZ_ASSERT(false, "unknown component");
      break;
  }
}

void
FilterNodeLinearTransferSoftware::FillLookupTableImpl(Float aSlope,
                                                      Float aIntercept,
                                                      uint8_t aTable[256])
{
  for (size_t i = 0; i < 256; i++) {
    int32_t val = NS_lround(aSlope * i + 255 * aIntercept);
    val = std::min(255, val);
    val = std::max(0, val);
    aTable[i] = val;
  }
}

FilterNodeGammaTransferSoftware::FilterNodeGammaTransferSoftware()
 : mAmplitudeR(0)
 , mAmplitudeG(0)
 , mAmplitudeB(0)
 , mAmplitudeA(0)
 , mExponentR(0)
 , mExponentG(0)
 , mExponentB(0)
 , mExponentA(0)
{}

void
FilterNodeGammaTransferSoftware::SetAttribute(uint32_t aIndex,
                                              Float aValue)
{
  switch (aIndex) {
    case ATT_GAMMA_TRANSFER_AMPLITUDE_R:
      mAmplitudeR = aValue;
      break;
    case ATT_GAMMA_TRANSFER_EXPONENT_R:
      mExponentR = aValue;
      break;
    case ATT_GAMMA_TRANSFER_OFFSET_R:
      mOffsetR = aValue;
      break;
    case ATT_GAMMA_TRANSFER_AMPLITUDE_G:
      mAmplitudeG = aValue;
      break;
    case ATT_GAMMA_TRANSFER_EXPONENT_G:
      mExponentG = aValue;
      break;
    case ATT_GAMMA_TRANSFER_OFFSET_G:
      mOffsetG = aValue;
      break;
    case ATT_GAMMA_TRANSFER_AMPLITUDE_B:
      mAmplitudeB = aValue;
      break;
    case ATT_GAMMA_TRANSFER_EXPONENT_B:
      mExponentB = aValue;
      break;
    case ATT_GAMMA_TRANSFER_OFFSET_B:
      mOffsetB = aValue;
      break;
    case ATT_GAMMA_TRANSFER_AMPLITUDE_A:
      mAmplitudeA = aValue;
      break;
    case ATT_GAMMA_TRANSFER_EXPONENT_A:
      mExponentA = aValue;
      break;
    case ATT_GAMMA_TRANSFER_OFFSET_A:
      mOffsetA = aValue;
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

void
FilterNodeGammaTransferSoftware::FillLookupTable(ptrdiff_t aComponent,
                                                 uint8_t aTable[256])
{
  switch (aComponent) {
    case B8G8R8A8_COMPONENT_BYTEOFFSET_R:
      FillLookupTableImpl(mAmplitudeR, mExponentR, mOffsetR, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_G:
      FillLookupTableImpl(mAmplitudeG, mExponentG, mOffsetG, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_B:
      FillLookupTableImpl(mAmplitudeB, mExponentB, mOffsetB, aTable);
      break;
    case B8G8R8A8_COMPONENT_BYTEOFFSET_A:
      FillLookupTableImpl(mAmplitudeA, mExponentA, mOffsetA, aTable);
      break;
    default:
      MOZ_ASSERT(false, "unknown component");
      break;
  }
}

void
FilterNodeGammaTransferSoftware::FillLookupTableImpl(Float aAmplitude,
                                                     Float aExponent,
                                                     Float aOffset,
                                                     uint8_t aTable[256])
{
  for (size_t i = 0; i < 256; i++) {
    int32_t val = NS_lround(255 * (aAmplitude * pow(i / 255.0f, aExponent) + aOffset));
    val = std::min(255, val);
    val = std::max(0, val);
    aTable[i] = val;
  }
}

FilterNodeConvolveMatrixSoftware::FilterNodeConvolveMatrixSoftware()
 : mDivisor(0)
 , mBias(0)
 , mEdgeMode(EDGE_MODE_DUPLICATE)
 , mPreserveAlpha(false)
{}

int32_t
FilterNodeConvolveMatrixSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_CONVOLVE_MATRIX_IN: return 0;
    default: return -1;
  }
}

void
FilterNodeConvolveMatrixSoftware::SetAttribute(uint32_t aIndex,
                                               const IntSize &aKernelSize)
{
  MOZ_ASSERT(aIndex == ATT_CONVOLVE_MATRIX_KERNEL_SIZE);
  mKernelSize = aKernelSize;
  Invalidate();
}

void
FilterNodeConvolveMatrixSoftware::SetAttribute(uint32_t aIndex,
                                               const Float *aMatrix,
                                               uint32_t aSize)
{
  MOZ_ASSERT(aIndex == ATT_CONVOLVE_MATRIX_KERNEL_MATRIX);
  mKernelMatrix = std::vector<Float>(aMatrix, aMatrix + aSize);
  Invalidate();
}

void
FilterNodeConvolveMatrixSoftware::SetAttribute(uint32_t aIndex, Float aValue)
{
  switch (aIndex) {
    case ATT_CONVOLVE_MATRIX_DIVISOR:
      mDivisor = aValue;
      break;
    case ATT_CONVOLVE_MATRIX_BIAS:
      mBias = aValue;
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

void
FilterNodeConvolveMatrixSoftware::SetAttribute(uint32_t aIndex, const Size &aKernelUnitLength)
{
  switch (aIndex) {
    case ATT_CONVOLVE_MATRIX_KERNEL_UNIT_LENGTH:
      mKernelUnitLength = aKernelUnitLength;
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

void
FilterNodeConvolveMatrixSoftware::SetAttribute(uint32_t aIndex,
                                               const IntPoint &aTarget)
{
  MOZ_ASSERT(aIndex == ATT_CONVOLVE_MATRIX_TARGET);
  mTarget = aTarget;
  Invalidate();
}

void
FilterNodeConvolveMatrixSoftware::SetAttribute(uint32_t aIndex,
                                               const IntRect &aSourceRect)
{
  MOZ_ASSERT(aIndex == ATT_CONVOLVE_MATRIX_SOURCE_RECT);
  mSourceRect = aSourceRect;
  Invalidate();
}

void
FilterNodeConvolveMatrixSoftware::SetAttribute(uint32_t aIndex,
                                               uint32_t aEdgeMode)
{
  MOZ_ASSERT(aIndex == ATT_CONVOLVE_MATRIX_EDGE_MODE);
  mEdgeMode = static_cast<ConvolveMatrixEdgeMode>(aEdgeMode);
  Invalidate();
}

void
FilterNodeConvolveMatrixSoftware::SetAttribute(uint32_t aIndex,
                                               bool aPreserveAlpha)
{
  MOZ_ASSERT(aIndex == ATT_CONVOLVE_MATRIX_PRESERVE_ALPHA);
  mPreserveAlpha = aPreserveAlpha;
  Invalidate();
}

static inline uint8_t
ColorComponentAtPoint(const uint8_t *aData, int32_t aStride, int32_t x, int32_t y, size_t bpp, ptrdiff_t c)
{
  return aData[y * aStride + bpp * x + c];
}

static inline int32_t
ColorAtPoint(const uint8_t *aData, int32_t aStride, int32_t x, int32_t y)
{
  return *(uint32_t*)(aData + y * aStride + 4 * x);
}

// Accepts fractional x & y and does bilinear interpolation.
// Only call this if the pixel (floor(x)+1, floor(y)+1) is accessible.
static inline uint8_t
ColorComponentAtPoint(const uint8_t *aData, int32_t aStride, Float x, Float y, size_t bpp, ptrdiff_t c)
{
  const uint32_t f = 256;
  const int32_t lx = floor(x);
  const int32_t ly = floor(y);
  const int32_t tux = uint32_t((x - lx) * f);
  const int32_t tlx = f - tux;
  const int32_t tuy = uint32_t((y - ly) * f);
  const int32_t tly = f - tuy;
  const uint8_t &cll = ColorComponentAtPoint(aData, aStride, lx,     ly,     bpp, c);
  const uint8_t &cul = ColorComponentAtPoint(aData, aStride, lx + 1, ly,     bpp, c);
  const uint8_t &clu = ColorComponentAtPoint(aData, aStride, lx,     ly + 1, bpp, c);
  const uint8_t &cuu = ColorComponentAtPoint(aData, aStride, lx + 1, ly + 1, bpp, c);
  return ((cll * tlx + cul * tux) * tly +
          (clu * tlx + cuu * tux) * tuy + f * f / 2) / (f * f);
}

static inline uint32_t
ColorAtPoint(const uint8_t *aData, int32_t aStride, Float x, Float y)
{
  return ColorComponentAtPoint(aData, aStride, x, y, 4, 0) |
         (ColorComponentAtPoint(aData, aStride, x, y, 4, 1) << 8) |
         (ColorComponentAtPoint(aData, aStride, x, y, 4, 2) << 16) |
         (ColorComponentAtPoint(aData, aStride, x, y, 4, 3) << 24);
}

static int32_t
ClampToNonZero(int32_t a)
{
  return a * (a >= 0);
}

template<typename CoordType>
static void
ConvolvePixel(const uint8_t *aSourceData,
              uint8_t *aTargetData,
              int32_t aWidth, int32_t aHeight,
              int32_t aSourceStride, int32_t aTargetStride,
              int32_t aX, int32_t aY,
              const int32_t *aKernel,
              int32_t aBias, int32_t shiftL, int32_t shiftR,
              bool aPreserveAlpha,
              int32_t aOrderX, int32_t aOrderY,
              int32_t aTargetX, int32_t aTargetY,
              CoordType aKernelUnitLengthX,
              CoordType aKernelUnitLengthY)
{
  int32_t sum[4] = {0, 0, 0, 0};
  int32_t offsets[4] = { B8G8R8A8_COMPONENT_BYTEOFFSET_R,
                         B8G8R8A8_COMPONENT_BYTEOFFSET_G,
                         B8G8R8A8_COMPONENT_BYTEOFFSET_B,
                         B8G8R8A8_COMPONENT_BYTEOFFSET_A };
  int32_t channels = aPreserveAlpha ? 3 : 4;
  int32_t roundingAddition = shiftL == 0 ? 0 : 1 << (shiftL - 1);

  for (int32_t y = 0; y < aOrderY; y++) {
    CoordType sampleY = aY + (y - aTargetY) * aKernelUnitLengthY;
    for (int32_t x = 0; x < aOrderX; x++) {
      CoordType sampleX = aX + (x - aTargetX) * aKernelUnitLengthX;
      for (int32_t i = 0; i < channels; i++) {
        sum[i] += aKernel[aOrderX * y + x] *
          ColorComponentAtPoint(aSourceData, aSourceStride,
                                sampleX, sampleY, 4, offsets[i]);
      }
    }
  }
  for (int32_t i = 0; i < channels; i++) {
    int32_t clamped = umin(ClampToNonZero(sum[i] + aBias), 255 << shiftL >> shiftR);
    aTargetData[aY * aTargetStride + 4 * aX + offsets[i]] =
      (clamped + roundingAddition) << shiftR >> shiftL;
  }
  if (aPreserveAlpha) {
    aTargetData[aY * aTargetStride + 4 * aX + B8G8R8A8_COMPONENT_BYTEOFFSET_A] =
      aSourceData[aY * aSourceStride + 4 * aX + B8G8R8A8_COMPONENT_BYTEOFFSET_A];
  }
}

TemporaryRef<DataSourceSurface>
FilterNodeConvolveMatrixSoftware::Render(const IntRect& aRect)
{
  if (mKernelUnitLength.width == floor(mKernelUnitLength.width) &&
      mKernelUnitLength.height == floor(mKernelUnitLength.height)) {
    return DoRender(aRect, (int32_t)mKernelUnitLength.width, (int32_t)mKernelUnitLength.height);
  }
  return DoRender(aRect, mKernelUnitLength.width, mKernelUnitLength.height);
}

static std::vector<Float>
ReversedVector(const std::vector<Float> &aVector)
{
  size_t length = aVector.size();
  std::vector<Float> result(length, 0);
  for (size_t i = 0; i < length; i++) {
    result[length - 1 - i] = aVector[i];
  }
  return result;
}

static std::vector<Float>
ScaledVector(const std::vector<Float> &aVector, Float aDivisor)
{
  size_t length = aVector.size();
  std::vector<Float> result(length, 0);
  for (size_t i = 0; i < length; i++) {
    result[i] = aVector[i] / aDivisor;
  }
  return result;
}

static Float
MaxVectorSum(const std::vector<Float> &aVector)
{
  Float sum = 0;
  size_t length = aVector.size();
  for (size_t i = 0; i < length; i++) {
    if (aVector[i] > 0) {
      sum += aVector[i];
    }
  }
  return sum;
}

// Returns shiftL and shiftR in such a way that
// a << shiftL >> shiftR is roughly a * aFloat.
static void
TranslateDoubleToShifts(double aDouble, int32_t &aShiftL, int32_t &aShiftR)
{
  aShiftL = 0;
  aShiftR = 0;
  if (aDouble <= 0) {
    MOZ_CRASH();
  }
  if (aDouble < 1) {
    while (1 << (aShiftR + 1) < 1 / aDouble) {
      aShiftR++;
    }
  } else {
    while (1 << (aShiftL + 1) < aDouble) {
      aShiftL++;
    }
  }
}

template<typename CoordType>
TemporaryRef<DataSourceSurface>
FilterNodeConvolveMatrixSoftware::DoRender(const IntRect& aRect,
                                           CoordType aKernelUnitLengthX,
                                           CoordType aKernelUnitLengthY)
{
  if (mKernelSize.width <= 0 || mKernelSize.height <= 0 ||
      mKernelMatrix.size() != uint32_t(mKernelSize.width * mKernelSize.height) ||
      !IntRect(IntPoint(0, 0), mKernelSize).Contains(mTarget) ||
      mDivisor == 0) {
    return Factory::CreateDataSourceSurface(aRect.Size(), FORMAT_B8G8R8A8);
  }

  IntRect srcRect = InflatedSourceRect(aRect);
  RefPtr<DataSourceSurface> input =
    GetInputDataSourceSurface(IN_CONVOLVE_MATRIX_IN, srcRect, NEED_COLOR_CHANNELS, mEdgeMode, &mSourceRect);
  RefPtr<DataSourceSurface> target =
    Factory::CreateDataSourceSurface(aRect.Size(), FORMAT_B8G8R8A8);
  if (!input || !target) {
    return nullptr;
  }
  ClearDataSourceSurface(target);

  uint8_t* sourceData = input->GetData();
  int32_t sourceStride = input->Stride();
  uint8_t* targetData = target->GetData();
  int32_t targetStride = target->Stride();

  IntPoint offset = aRect.TopLeft() - srcRect.TopLeft();
  sourceData += DataOffset(input, offset);

  // Why exactly are we reversing the kernel?
  std::vector<Float> kernel = ReversedVector(mKernelMatrix);
  kernel = ScaledVector(kernel, mDivisor);
  Float maxResultAbs = std::max(MaxVectorSum(kernel) + mBias,
                                MaxVectorSum(ScaledVector(kernel, -1)) - mBias);
  maxResultAbs = std::max(maxResultAbs, 1.0f);

  double idealFactor = INT32_MAX / 2.0 / maxResultAbs / 255.0 * 0.999;
  MOZ_ASSERT(255.0 * maxResultAbs * idealFactor <= INT32_MAX / 2.0, "badly chosen float-to-int scale");
  int32_t shiftL, shiftR;
  TranslateDoubleToShifts(idealFactor, shiftL, shiftR);
  double factorFromShifts = Float(1 << shiftL) / Float(1 << shiftR);
  MOZ_ASSERT(255.0 * maxResultAbs * factorFromShifts <= INT32_MAX / 2.0, "badly chosen float-to-int scale");

  int32_t* intKernel = new int32_t[kernel.size()];
  for (size_t i = 0; i < kernel.size(); i++) {
    intKernel[i] = NS_lround(kernel[i] * factorFromShifts);
  }
  int32_t bias = NS_lround(mBias * 255 * factorFromShifts);

  for (int32_t y = 0; y < aRect.height; y++) {
    for (int32_t x = 0; x < aRect.width; x++) {
      ConvolvePixel(sourceData, targetData,
                    aRect.width, aRect.height, sourceStride, targetStride,
                    x, y, intKernel, bias, shiftL, shiftR, mPreserveAlpha,
                    mKernelSize.width, mKernelSize.height, mTarget.x, mTarget.y,
                    aKernelUnitLengthX, aKernelUnitLengthY);
    }
  }
  delete[] intKernel;

  return target;
}

void
FilterNodeConvolveMatrixSoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  RequestInputRect(IN_CONVOLVE_MATRIX_IN, InflatedSourceRect(aRect));
}

IntRect
FilterNodeConvolveMatrixSoftware::InflatedSourceRect(const IntRect &aDestRect)
{
  if (aDestRect.IsEmpty()) {
    return IntRect();
  }

  IntMargin margin;
  margin.left = ceil(mTarget.x * mKernelUnitLength.width);
  margin.top = ceil(mTarget.y * mKernelUnitLength.height);
  margin.right = ceil((mKernelSize.width - mTarget.x - 1) * mKernelUnitLength.width);
  margin.bottom = ceil((mKernelSize.height - mTarget.y - 1) * mKernelUnitLength.height);

  IntRect srcRect = aDestRect;
  srcRect.Inflate(margin);
  return srcRect;
}

IntRect
FilterNodeConvolveMatrixSoftware::InflatedDestRect(const IntRect &aSourceRect)
{
  if (aSourceRect.IsEmpty()) {
    return IntRect();
  }

  IntMargin margin;
  margin.left = ceil((mKernelSize.width - mTarget.x - 1) * mKernelUnitLength.width);
  margin.top = ceil((mKernelSize.height - mTarget.y - 1) * mKernelUnitLength.height);
  margin.right = ceil(mTarget.x * mKernelUnitLength.width);
  margin.bottom = ceil(mTarget.y * mKernelUnitLength.height);

  IntRect destRect = aSourceRect;
  destRect.Inflate(margin);
  return destRect;
}

IntRect
FilterNodeConvolveMatrixSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  IntRect srcRequest = InflatedSourceRect(aRect);
  IntRect srcOutput = GetInputRectInRect(IN_COLOR_MATRIX_IN, srcRequest);
  return InflatedDestRect(srcOutput).Intersect(aRect);
}

FilterNodeDisplacementMapSoftware::FilterNodeDisplacementMapSoftware()
 : mScale(0.0f)
 , mChannelX(COLOR_CHANNEL_R)
 , mChannelY(COLOR_CHANNEL_G)
{}

int32_t
FilterNodeDisplacementMapSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_DISPLACEMENT_MAP_IN: return 0;
    case IN_DISPLACEMENT_MAP_IN2: return 1;
    default: return -1;
  }
}

void
FilterNodeDisplacementMapSoftware::SetAttribute(uint32_t aIndex,
                                                Float aScale)
{
  MOZ_ASSERT(aIndex == ATT_DISPLACEMENT_MAP_SCALE);
  mScale = aScale;
  Invalidate();
}

void
FilterNodeDisplacementMapSoftware::SetAttribute(uint32_t aIndex, uint32_t aValue)
{
  switch (aIndex) {
    case ATT_DISPLACEMENT_MAP_X_CHANNEL:
      mChannelX = static_cast<ColorChannel>(aValue);
      break;
    case ATT_DISPLACEMENT_MAP_Y_CHANNEL:
      mChannelY = static_cast<ColorChannel>(aValue);
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

TemporaryRef<DataSourceSurface>
FilterNodeDisplacementMapSoftware::Render(const IntRect& aRect)
{
  IntRect srcRect = InflatedSourceOrDestRect(aRect);
  RefPtr<DataSourceSurface> input =
    GetInputDataSourceSurface(IN_DISPLACEMENT_MAP_IN, srcRect, NEED_COLOR_CHANNELS);
  RefPtr<DataSourceSurface> map =
    GetInputDataSourceSurface(IN_DISPLACEMENT_MAP_IN2, aRect, NEED_COLOR_CHANNELS);
  RefPtr<DataSourceSurface> target =
    Factory::CreateDataSourceSurface(aRect.Size(), FORMAT_B8G8R8A8);
  if (!input || !map || !target) {
    return nullptr;
  }

  uint8_t* sourceData = input->GetData();
  int32_t sourceStride = input->Stride();
  uint8_t* mapData = map->GetData();
  int32_t mapStride = map->Stride();
  uint8_t* targetData = target->GetData();
  int32_t targetStride = target->Stride();

  IntPoint offset = aRect.TopLeft() - srcRect.TopLeft();
  sourceData += DataOffset(input, offset);

  static const ptrdiff_t channelMap[4] = {
                             B8G8R8A8_COMPONENT_BYTEOFFSET_R,
                             B8G8R8A8_COMPONENT_BYTEOFFSET_G,
                             B8G8R8A8_COMPONENT_BYTEOFFSET_B,
                             B8G8R8A8_COMPONENT_BYTEOFFSET_A };
  uint16_t xChannel = channelMap[mChannelX];
  uint16_t yChannel = channelMap[mChannelY];

  float scaleOver255 = mScale / 255.0f;
  float scaleAdjustment = -0.5f * mScale;

  for (int32_t y = 0; y < aRect.height; y++) {
    for (int32_t x = 0; x < aRect.width; x++) {
      uint32_t mapIndex = y * mapStride + 4 * x;
      uint32_t targIndex = y * targetStride + 4 * x;
      int32_t sourceX = x +
        scaleOver255 * mapData[mapIndex + xChannel] + scaleAdjustment;
      int32_t sourceY = y +
        scaleOver255 * mapData[mapIndex + yChannel] + scaleAdjustment;
      *(uint32_t*)(targetData + targIndex) =
        ColorAtPoint(sourceData, sourceStride, sourceX, sourceY);
    }
  }

  return target;
}

void
FilterNodeDisplacementMapSoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  RequestInputRect(IN_DISPLACEMENT_MAP_IN, InflatedSourceOrDestRect(aRect));
  RequestInputRect(IN_DISPLACEMENT_MAP_IN2, aRect);
}

IntRect
FilterNodeDisplacementMapSoftware::InflatedSourceOrDestRect(const IntRect &aDestOrSourceRect)
{
  IntRect sourceOrDestRect = aDestOrSourceRect;
  sourceOrDestRect.Inflate(ceil(fabs(mScale) / 2));
  return sourceOrDestRect;
}

IntRect
FilterNodeDisplacementMapSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  IntRect srcRequest = InflatedSourceOrDestRect(aRect);
  IntRect srcOutput = GetInputRectInRect(IN_DISPLACEMENT_MAP_IN, srcRequest);
  return InflatedSourceOrDestRect(srcOutput).Intersect(aRect);
}

FilterNodeTurbulenceSoftware::FilterNodeTurbulenceSoftware()
 : mNumOctaves(0)
 , mSeed(0)
 , mStitchable(false)
 , mType(TURBULENCE_TYPE_TURBULENCE)
{}

int32_t
FilterNodeTurbulenceSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  return -1;
}

void
FilterNodeTurbulenceSoftware::SetAttribute(uint32_t aIndex, const Size &aBaseFrequency)
{
  switch (aIndex) {
    case ATT_TURBULENCE_BASE_FREQUENCY:
      mBaseFrequency = aBaseFrequency;
      break;
    default:
      MOZ_CRASH();
      break;
  }
  Invalidate();
}

void
FilterNodeTurbulenceSoftware::SetAttribute(uint32_t aIndex, const IntRect &aRect)
{
  switch (aIndex) {
    case ATT_TURBULENCE_RECT:
      mRenderRect = aRect;
      break;
    default:
      MOZ_CRASH();
      break;
  }
  Invalidate();
}

void
FilterNodeTurbulenceSoftware::SetAttribute(uint32_t aIndex, bool aStitchable)
{
  MOZ_ASSERT(aIndex == ATT_TURBULENCE_STITCHABLE);
  mStitchable = aStitchable;
  Invalidate();
}

void
FilterNodeTurbulenceSoftware::SetAttribute(uint32_t aIndex, uint32_t aValue)
{
  switch (aIndex) {
    case ATT_TURBULENCE_NUM_OCTAVES:
      mNumOctaves = aValue;
      break;
    case ATT_TURBULENCE_SEED:
      mSeed = aValue;
      break;
    case ATT_TURBULENCE_TYPE:
      mType = static_cast<TurbulenceType>(aValue);
      break;
    default:
      MOZ_CRASH();
      break;
  }
  Invalidate();
}

TemporaryRef<DataSourceSurface>
FilterNodeTurbulenceSoftware::Render(const IntRect& aRect)
{
  return FilterProcessing::RenderTurbulence(
    aRect.Size(), aRect.TopLeft(), mBaseFrequency,
    mSeed, mNumOctaves, mType, mStitchable, Rect(mRenderRect));
}

IntRect
FilterNodeTurbulenceSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  return aRect.Intersect(mRenderRect);
}

FilterNodeArithmeticCombineSoftware::FilterNodeArithmeticCombineSoftware()
 : mK1(0), mK2(0), mK3(0), mK4(0)
{
}

int32_t
FilterNodeArithmeticCombineSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_ARITHMETIC_COMBINE_IN: return 0;
    case IN_ARITHMETIC_COMBINE_IN2: return 1;
    default: return -1;
  }
}

void
FilterNodeArithmeticCombineSoftware::SetAttribute(uint32_t aIndex,
                                                  const Float* aFloat,
                                                  uint32_t aSize)
{
  MOZ_ASSERT(aIndex == ATT_ARITHMETIC_COMBINE_COEFFICIENTS);
  MOZ_ASSERT(aSize == 4);

  mK1 = aFloat[0];
  mK2 = aFloat[1];
  mK3 = aFloat[2];
  mK4 = aFloat[3];

  Invalidate();
}

TemporaryRef<DataSourceSurface>
FilterNodeArithmeticCombineSoftware::Render(const IntRect& aRect)
{
  RefPtr<DataSourceSurface> input1 =
    GetInputDataSourceSurface(IN_ARITHMETIC_COMBINE_IN, aRect, NEED_COLOR_CHANNELS);
  RefPtr<DataSourceSurface> input2 =
    GetInputDataSourceSurface(IN_ARITHMETIC_COMBINE_IN2, aRect, NEED_COLOR_CHANNELS);
  if (!input1 && !input2) {
    return nullptr;
  }

  // If one input is null, treat it as transparent by adjusting the factors.
  Float k1 = mK1, k2 = mK2, k3 = mK3, k4 = mK4;
  if (!input1) {
    k1 = 0.0f;
    k2 = 0.0f;
    input1 = input2;
  }

  if (!input2) {
    k1 = 0.0f;
    k3 = 0.0f;
    input2 = input1;
  }

  return FilterProcessing::ApplyArithmeticCombine(input1, input2, k1, k2, k3, k4);
}

void
FilterNodeArithmeticCombineSoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  RequestInputRect(IN_ARITHMETIC_COMBINE_IN, aRect);
  RequestInputRect(IN_ARITHMETIC_COMBINE_IN2, aRect);
}

IntRect
FilterNodeArithmeticCombineSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  return GetInputRectInRect(IN_ARITHMETIC_COMBINE_IN, aRect).Union(
    GetInputRectInRect(IN_ARITHMETIC_COMBINE_IN2, aRect)).Intersect(aRect);
}

FilterNodeCompositeSoftware::FilterNodeCompositeSoftware()
 : mOperator(COMPOSITE_OPERATOR_OVER)
{}

int32_t
FilterNodeCompositeSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  return aInputEnumIndex - IN_COMPOSITE_IN_START;
}

void
FilterNodeCompositeSoftware::SetAttribute(uint32_t aIndex, uint32_t aCompositeOperator)
{
  MOZ_ASSERT(aIndex == ATT_COMPOSITE_OPERATOR);
  mOperator = static_cast<CompositeOperator>(aCompositeOperator);
  Invalidate();
}

TemporaryRef<DataSourceSurface>
FilterNodeCompositeSoftware::Render(const IntRect& aRect)
{
  RefPtr<DataSourceSurface> start =
    GetInputDataSourceSurface(IN_COMPOSITE_IN_START, aRect, NEED_COLOR_CHANNELS);
  RefPtr<DataSourceSurface> dest =
    Factory::CreateDataSourceSurface(aRect.Size(), FORMAT_B8G8R8A8);
  if (!dest) {
    return nullptr;
  }

  if (start) {
    CopyRect(start, dest, aRect - aRect.TopLeft(), IntPoint());
  } else {
    ClearDataSourceSurface(dest);
  }

  for (size_t inputIndex = 1; inputIndex < NumberOfSetInputs(); inputIndex++) {
    RefPtr<DataSourceSurface> input =
      GetInputDataSourceSurface(IN_COMPOSITE_IN_START + inputIndex, aRect, NEED_COLOR_CHANNELS);
    if (input) {
      FilterProcessing::ApplyComposition(input, dest, mOperator);
    } else {
      // We need to treat input as transparent. Depending on the composite
      // operator, different things happen to dest.
      switch (mOperator) {
        case COMPOSITE_OPERATOR_OVER:
        case COMPOSITE_OPERATOR_ATOP:
        case COMPOSITE_OPERATOR_XOR:
          // dest is unchanged.
          break;
        case COMPOSITE_OPERATOR_OUT:
          // dest is now transparent, but it can become non-transparent again
          // when compositing additional inputs.
          ClearDataSourceSurface(dest);
          break;
        case COMPOSITE_OPERATOR_IN:
          // Transparency always wins. We're completely transparent now and
          // no additional input can get rid of that transparency.
          return nullptr;
      }
    }
  }
  return dest;
}

void
FilterNodeCompositeSoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  for (size_t inputIndex = 0; inputIndex < NumberOfSetInputs(); inputIndex++) {
    RequestInputRect(IN_COMPOSITE_IN_START + inputIndex, aRect);
  }
}

IntRect
FilterNodeCompositeSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  IntRect rect;
  for (size_t inputIndex = 0; inputIndex < NumberOfSetInputs(); inputIndex++) {
    rect = rect.Union(GetInputRectInRect(IN_COMPOSITE_IN_START + inputIndex, aRect));
  }
  return rect;
}

int32_t
FilterNodeBlurXYSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_GAUSSIAN_BLUR_IN: return 0;
    default: return -1;
  }
}

TemporaryRef<DataSourceSurface>
FilterNodeBlurXYSoftware::Render(const IntRect& aRect)
{
  Size sigmaXY = StdDeviationXY();
  IntSize d = AlphaBoxBlur::CalculateBlurRadius(Point(sigmaXY.width, sigmaXY.height));

  if (d.width == 0 && d.height == 0) {
    return GetInputDataSourceSurface(IN_GAUSSIAN_BLUR_IN, aRect);
  }

  IntRect srcRect = InflatedSourceOrDestRect(aRect);
  RefPtr<DataSourceSurface> input =
    GetInputDataSourceSurface(IN_GAUSSIAN_BLUR_IN, srcRect);
  if (!input) {
    return nullptr;
  }

  RefPtr<DataSourceSurface> target;
  Rect r(0, 0, srcRect.width, srcRect.height);

  if (input->GetFormat() == FORMAT_A8) {
    target = Factory::CreateDataSourceSurface(srcRect.Size(), FORMAT_A8);
    CopyRect(input, target, IntRect(IntPoint(), input->GetSize()), IntPoint());
    AlphaBoxBlur blur(r, target->Stride(), sigmaXY.width, sigmaXY.height);
    blur.Blur(target->GetData());
  } else {
    RefPtr<DataSourceSurface> channel0, channel1, channel2, channel3;
    FilterProcessing::SeparateColorChannels(input, channel0, channel1, channel2, channel3);
    AlphaBoxBlur blur(r, channel0->Stride(), sigmaXY.width, sigmaXY.height);
    blur.Blur(channel0->GetData());
    blur.Blur(channel1->GetData());
    blur.Blur(channel2->GetData());
    blur.Blur(channel3->GetData());
    target = FilterProcessing::CombineColorChannels(channel0, channel1, channel2, channel3);
  }

  return GetDataSurfaceInRect(target, srcRect, aRect, EDGE_MODE_NONE);
}

void
FilterNodeBlurXYSoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  RequestInputRect(IN_GAUSSIAN_BLUR_IN, InflatedSourceOrDestRect(aRect));
}

IntRect
FilterNodeBlurXYSoftware::InflatedSourceOrDestRect(const IntRect &aDestRect)
{
  Size sigmaXY = StdDeviationXY();
  IntSize d = AlphaBoxBlur::CalculateBlurRadius(Point(sigmaXY.width, sigmaXY.height));
  IntRect srcRect = aDestRect;
  srcRect.Inflate(d);
  return srcRect;
}

IntRect
FilterNodeBlurXYSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  IntRect srcRequest = InflatedSourceOrDestRect(aRect);
  IntRect srcOutput = GetInputRectInRect(IN_GAUSSIAN_BLUR_IN, srcRequest);
  return InflatedSourceOrDestRect(srcOutput).Intersect(aRect);
}

FilterNodeGaussianBlurSoftware::FilterNodeGaussianBlurSoftware()
 : mStdDeviation(0)
{}

void
FilterNodeGaussianBlurSoftware::SetAttribute(uint32_t aIndex,
                                             float aStdDeviation)
{
  switch (aIndex) {
    case ATT_GAUSSIAN_BLUR_STD_DEVIATION:
      mStdDeviation = std::max(0.0f, aStdDeviation);
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

Size
FilterNodeGaussianBlurSoftware::StdDeviationXY()
{
  return Size(mStdDeviation, mStdDeviation);
}

FilterNodeDirectionalBlurSoftware::FilterNodeDirectionalBlurSoftware()
 : mBlurDirection(BLUR_DIRECTION_X)
{}

void
FilterNodeDirectionalBlurSoftware::SetAttribute(uint32_t aIndex,
                                                Float aStdDeviation)
{
  switch (aIndex) {
    case ATT_DIRECTIONAL_BLUR_STD_DEVIATION:
      mStdDeviation = std::max(0.0f, aStdDeviation);
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

void
FilterNodeDirectionalBlurSoftware::SetAttribute(uint32_t aIndex,
                                                uint32_t aBlurDirection)
{
  switch (aIndex) {
    case ATT_DIRECTIONAL_BLUR_DIRECTION:
      mBlurDirection = (BlurDirection)aBlurDirection;
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

Size
FilterNodeDirectionalBlurSoftware::StdDeviationXY()
{
  float sigmaX = mBlurDirection == BLUR_DIRECTION_X ? mStdDeviation : 0;
  float sigmaY = mBlurDirection == BLUR_DIRECTION_Y ? mStdDeviation : 0;
  return Size(sigmaX, sigmaY);
}

int32_t
FilterNodeCropSoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_CROP_IN: return 0;
    default: return -1;
  }
}

void
FilterNodeCropSoftware::SetAttribute(uint32_t aIndex,
                                     const Rect &aSourceRect)
{
  MOZ_ASSERT(aIndex == ATT_CROP_RECT);
  Rect srcRect = aSourceRect;
  srcRect.Round();
  mCropRect = IntRect(int32_t(srcRect.x), int32_t(srcRect.y),
                      int32_t(srcRect.width), int32_t(srcRect.height));
  Invalidate();
}

TemporaryRef<DataSourceSurface>
FilterNodeCropSoftware::Render(const IntRect& aRect)
{
  return GetInputDataSourceSurface(IN_CROP_IN, aRect.Intersect(mCropRect));
}

void
FilterNodeCropSoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  RequestInputRect(IN_CROP_IN, aRect.Intersect(mCropRect));
}

IntRect
FilterNodeCropSoftware::GetOutputRectInRect(const IntRect& aRect)
{
  return GetInputRectInRect(IN_CROP_IN, aRect).Intersect(mCropRect);
}

int32_t
FilterNodePremultiplySoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_PREMULTIPLY_IN: return 0;
    default: return -1;
  }
}

TemporaryRef<DataSourceSurface>
FilterNodePremultiplySoftware::Render(const IntRect& aRect)
{
  RefPtr<DataSourceSurface> input =
    GetInputDataSourceSurface(IN_PREMULTIPLY_IN, aRect);
  return input ? Premultiply(input) : nullptr;
}

void
FilterNodePremultiplySoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  RequestInputRect(IN_PREMULTIPLY_IN, aRect);
}

IntRect
FilterNodePremultiplySoftware::GetOutputRectInRect(const IntRect& aRect)
{
  return GetInputRectInRect(IN_PREMULTIPLY_IN, aRect);
}

int32_t
FilterNodeUnpremultiplySoftware::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_UNPREMULTIPLY_IN: return 0;
    default: return -1;
  }
}

TemporaryRef<DataSourceSurface>
FilterNodeUnpremultiplySoftware::Render(const IntRect& aRect)
{
  RefPtr<DataSourceSurface> input =
    GetInputDataSourceSurface(IN_UNPREMULTIPLY_IN, aRect);
  return input ? Unpremultiply(input) : nullptr;
}

void
FilterNodeUnpremultiplySoftware::RequestFromInputsForRect(const IntRect &aRect)
{
  RequestInputRect(IN_UNPREMULTIPLY_IN, aRect);
}

IntRect
FilterNodeUnpremultiplySoftware::GetOutputRectInRect(const IntRect& aRect)
{
  return GetInputRectInRect(IN_UNPREMULTIPLY_IN, aRect);
}

bool
PointLightSoftware::SetAttribute(uint32_t aIndex, const Point3D &aPoint)
{
  switch (aIndex) {
    case ATT_POINT_LIGHT_POSITION:
      mPosition = aPoint;
      break;
    default:
      return false;
  }
  return true;
}

SpotLightSoftware::SpotLightSoftware()
 : mSpecularFocus(0)
 , mLimitingConeAngle(0)
 , mLimitingConeCos(1)
{
}

bool
SpotLightSoftware::SetAttribute(uint32_t aIndex, const Point3D &aPoint)
{
  switch (aIndex) {
    case ATT_SPOT_LIGHT_POSITION:
      mPosition = aPoint;
      break;
    case ATT_SPOT_LIGHT_POINTS_AT:
      mPointsAt = aPoint;
      break;
    default:
      return false;
  }
  return true;
}

bool
SpotLightSoftware::SetAttribute(uint32_t aIndex, Float aValue)
{
  switch (aIndex) {
    case ATT_SPOT_LIGHT_LIMITING_CONE_ANGLE:
      mLimitingConeAngle = aValue;
      break;
    case ATT_SPOT_LIGHT_FOCUS:
      mSpecularFocus = aValue;
      break;
    default:
      return false;
  }
  return true;
}

DistantLightSoftware::DistantLightSoftware()
 : mAzimuth(0)
 , mElevation(0)
{
}

bool
DistantLightSoftware::SetAttribute(uint32_t aIndex, Float aValue)
{
  switch (aIndex) {
    case ATT_DISTANT_LIGHT_AZIMUTH:
      mAzimuth = aValue;
      break;
    case ATT_DISTANT_LIGHT_ELEVATION:
      mElevation = aValue;
      break;
    default:
      return false;
  }
  return true;
}

static inline Point3D Normalized(const Point3D &vec) {
  Point3D copy(vec);
  copy.Normalize();
  return copy;
}

template<typename LightType, typename LightingType>
FilterNodeLightingSoftware<LightType, LightingType>::FilterNodeLightingSoftware()
 : mSurfaceScale(0)
{}

template<typename LightType, typename LightingType>
int32_t
FilterNodeLightingSoftware<LightType, LightingType>::InputIndex(uint32_t aInputEnumIndex)
{
  switch (aInputEnumIndex) {
    case IN_LIGHTING_IN: return 0;
    default: return -1;
  }
}

template<typename LightType, typename LightingType>
void
FilterNodeLightingSoftware<LightType, LightingType>::SetAttribute(uint32_t aIndex, const Point3D &aPoint)
{
  if (mLight.SetAttribute(aIndex, aPoint)) {
    Invalidate();
    return;
  }
  MOZ_CRASH();
}

template<typename LightType, typename LightingType>
void
FilterNodeLightingSoftware<LightType, LightingType>::SetAttribute(uint32_t aIndex, Float aValue)
{
  if (mLight.SetAttribute(aIndex, aValue) ||
      mLighting.SetAttribute(aIndex, aValue)) {
    Invalidate();
    return;
  }
  switch (aIndex) {
    case ATT_LIGHTING_SURFACE_SCALE:
      mSurfaceScale = aValue;
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

template<typename LightType, typename LightingType>
void
FilterNodeLightingSoftware<LightType, LightingType>::SetAttribute(uint32_t aIndex, const Size &aKernelUnitLength)
{
  switch (aIndex) {
    case ATT_LIGHTING_KERNEL_UNIT_LENGTH:
      mKernelUnitLength = aKernelUnitLength;
      break;
    default:
      MOZ_CRASH();
  }
  Invalidate();
}

template<typename LightType, typename LightingType>
void
FilterNodeLightingSoftware<LightType, LightingType>::SetAttribute(uint32_t aIndex, const Color &aColor)
{
  MOZ_ASSERT(aIndex == ATT_LIGHTING_COLOR);
  mColor = aColor;
  Invalidate();
}

template<typename LightType, typename LightingType>
IntRect
FilterNodeLightingSoftware<LightType, LightingType>::GetOutputRectInRect(const IntRect& aRect)
{
  return GetInputRectInRect(IN_LIGHTING_IN, aRect);
}

Point3D
PointLightSoftware::GetVectorToLight(const Point3D &aTargetPoint)
{
  return Normalized(mPosition - aTargetPoint);
}

uint32_t
PointLightSoftware::GetColor(uint32_t aLightColor, const Point3D &aVectorToLight)
{
  return aLightColor;
}

void
SpotLightSoftware::Prepare()
{
  mVectorFromFocusPointToLight = Normalized(mPointsAt - mPosition);
  const float radPerDeg = static_cast<float>(M_PI/180.0);
  mLimitingConeCos = std::max<double>(cos(mLimitingConeAngle * radPerDeg), 0.0);
  mPowCache.CacheForExponent(mSpecularFocus);
}

Point3D
SpotLightSoftware::GetVectorToLight(const Point3D &aTargetPoint)
{
  return Normalized(mPosition - aTargetPoint);
}

uint32_t
SpotLightSoftware::GetColor(uint32_t aLightColor, const Point3D &aVectorToLight)
{
  union {
    uint32_t color;
    uint8_t colorC[4];
  };
  color = aLightColor;
  Float dot = -aVectorToLight.DotProduct(mVectorFromFocusPointToLight);
  int16_t doti = dot * (255 << 7);
  uint16_t tmp = mPowCache.Pow(doti) * (dot >= mLimitingConeCos);
  colorC[B8G8R8A8_COMPONENT_BYTEOFFSET_R] = uint8_t((colorC[B8G8R8A8_COMPONENT_BYTEOFFSET_R] * tmp) >> 8);
  colorC[B8G8R8A8_COMPONENT_BYTEOFFSET_G] = uint8_t((colorC[B8G8R8A8_COMPONENT_BYTEOFFSET_G] * tmp) >> 8);
  colorC[B8G8R8A8_COMPONENT_BYTEOFFSET_B] = uint8_t((colorC[B8G8R8A8_COMPONENT_BYTEOFFSET_B] * tmp) >> 8);
  colorC[B8G8R8A8_COMPONENT_BYTEOFFSET_A] = 255;
  return color;
}

void
DistantLightSoftware::Prepare()
{
  const double radPerDeg = M_PI / 180.0;
  mVectorToLight.x = cos(mAzimuth * radPerDeg) * cos(mElevation * radPerDeg);
  mVectorToLight.y = sin(mAzimuth * radPerDeg) * cos(mElevation * radPerDeg);
  mVectorToLight.z = sin(mElevation * radPerDeg);
}

Point3D
DistantLightSoftware::GetVectorToLight(const Point3D &aTargetPoint)
{
  return mVectorToLight;
}

uint32_t
DistantLightSoftware::GetColor(uint32_t aLightColor, const Point3D &aVectorToLight)
{
  return aLightColor;
}

template<typename CoordType>
static Point3D
GenerateNormal(const uint8_t *data, int32_t stride,
               int32_t x, int32_t y, float surfaceScale,
               CoordType dx, CoordType dy)
{
  const uint8_t *index = data + y * stride + x;

  CoordType zero = 0;

  // See this for source of constants:
  //   http://www.w3.org/TR/SVG11/filters.html#feDiffuseLightingElement
  int16_t normalX =
    -1 * ColorComponentAtPoint(index, stride, -dx, -dy, 1, 0) +
     1 * ColorComponentAtPoint(index, stride, dx, -dy, 1, 0) +
    -2 * ColorComponentAtPoint(index, stride, -dx, zero, 1, 0) +
     2 * ColorComponentAtPoint(index, stride, dx, zero, 1, 0) +
    -1 * ColorComponentAtPoint(index, stride, -dx, dy, 1, 0) +
     1 * ColorComponentAtPoint(index, stride, dx, dy, 1, 0);

  int16_t normalY =
    -1 * ColorComponentAtPoint(index, stride, -dx, -dy, 1, 0) +
    -2 * ColorComponentAtPoint(index, stride, zero, -dy, 1, 0) +
    -1 * ColorComponentAtPoint(index, stride, dx, -dy, 1, 0) +
     1 * ColorComponentAtPoint(index, stride, -dx, dy, 1, 0) +
     2 * ColorComponentAtPoint(index, stride, zero, dy, 1, 0) +
     1 * ColorComponentAtPoint(index, stride, dx, dy, 1, 0);

  Point3D normal;
  normal.x = -surfaceScale * normalX / 4.0f;
  normal.y = -surfaceScale * normalY / 4.0f;
  normal.z = 255;
  return Normalized(normal);
}

template<typename LightType, typename LightingType>
TemporaryRef<DataSourceSurface>
FilterNodeLightingSoftware<LightType, LightingType>::Render(const IntRect& aRect)
{
  if (mKernelUnitLength.width == floor(mKernelUnitLength.width) &&
      mKernelUnitLength.height == floor(mKernelUnitLength.height)) {
    return DoRender(aRect, (int32_t)mKernelUnitLength.width, (int32_t)mKernelUnitLength.height);
  }
  return DoRender(aRect, mKernelUnitLength.width, mKernelUnitLength.height);
}

template<typename LightType, typename LightingType>
void
FilterNodeLightingSoftware<LightType, LightingType>::RequestFromInputsForRect(const IntRect &aRect)
{
  IntRect srcRect = aRect;
  srcRect.Inflate(ceil(mKernelUnitLength.width),
                  ceil(mKernelUnitLength.height));
  RequestInputRect(IN_LIGHTING_IN, srcRect);
}

template<typename LightType, typename LightingType> template<typename CoordType>
TemporaryRef<DataSourceSurface>
FilterNodeLightingSoftware<LightType, LightingType>::DoRender(const IntRect& aRect,
                                                              CoordType aKernelUnitLengthX,
                                                              CoordType aKernelUnitLengthY)
{
  IntRect srcRect = aRect;
  IntSize size = aRect.Size();
  srcRect.Inflate(ceil(float(aKernelUnitLengthX)),
                  ceil(float(aKernelUnitLengthY)));
  RefPtr<DataSourceSurface> input =
    GetInputDataSourceSurface(IN_LIGHTING_IN, srcRect, CAN_HANDLE_A8,
                              EDGE_MODE_DUPLICATE);

  if (!input) {
    return nullptr;
  }

  if (input->GetFormat() != FORMAT_A8) {
    input = FilterProcessing::ExtractAlpha(input);
  }

  RefPtr<DataSourceSurface> target =
    Factory::CreateDataSourceSurface(size, FORMAT_B8G8R8A8);
  if (!target) {
    return nullptr;
  }

  uint8_t* sourceData = input->GetData();
  int32_t sourceStride = input->Stride();
  uint8_t* targetData = target->GetData();
  int32_t targetStride = target->Stride();

  IntPoint offset = aRect.TopLeft() - srcRect.TopLeft();
  sourceData += DataOffset(input, offset);

  uint32_t lightColor = ColorToBGRA(mColor);
  mLight.Prepare();
  mLighting.Prepare();

  for (int32_t y = 0; y < size.height; y++) {
    for (int32_t x = 0; x < size.width; x++) {
      int32_t sourceIndex = y * sourceStride + x;
      int32_t targetIndex = y * targetStride + 4 * x;

      Point3D normal = GenerateNormal(sourceData, sourceStride,
                                      x, y, mSurfaceScale,
                                      aKernelUnitLengthX, aKernelUnitLengthY);

      IntPoint pointInFilterSpace(aRect.x + x, aRect.y + y);
      Float Z = mSurfaceScale * sourceData[sourceIndex] / 255.0f;
      Point3D pt(pointInFilterSpace.x, pointInFilterSpace.y, Z);
      Point3D rayDir = mLight.GetVectorToLight(pt);
      uint32_t color = mLight.GetColor(lightColor, rayDir);

      *(uint32_t*)(targetData + targetIndex) = mLighting.LightPixel(normal, rayDir, color);
    }
  }

  return target;
}

DiffuseLightingSoftware::DiffuseLightingSoftware()
 : mDiffuseConstant(0)
{
}

bool
DiffuseLightingSoftware::SetAttribute(uint32_t aIndex, Float aValue)
{
  switch (aIndex) {
    case ATT_DIFFUSE_LIGHTING_DIFFUSE_CONSTANT:
      mDiffuseConstant = aValue;
      break;
    default:
      return false;
  }
  return true;
}

uint32_t
DiffuseLightingSoftware::LightPixel(const Point3D &aNormal,
                                    const Point3D &aVectorToLight,
                                    uint32_t aColor)
{
  Float dotNL = std::max(0.0f, aNormal.DotProduct(aVectorToLight));
  Float diffuseNL = mDiffuseConstant * dotNL;

  union {
    uint32_t bgra;
    uint8_t components[4];
  } color = { aColor };
  color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_B] =
    umin(uint32_t(diffuseNL * color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_B]), 255U);
  color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_G] =
    umin(uint32_t(diffuseNL * color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_G]), 255U);
  color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_R] =
    umin(uint32_t(diffuseNL * color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_R]), 255U);
  color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_A] = 255;
  return color.bgra;
}

SpecularLightingSoftware::SpecularLightingSoftware()
 : mSpecularConstant(0)
 , mSpecularExponent(0)
{
}

bool
SpecularLightingSoftware::SetAttribute(uint32_t aIndex, Float aValue)
{
  switch (aIndex) {
    case ATT_SPECULAR_LIGHTING_SPECULAR_CONSTANT:
      mSpecularConstant = std::min(std::max(aValue, 0.0f), 255.0f);
      break;
    case ATT_SPECULAR_LIGHTING_SPECULAR_EXPONENT:
      mSpecularExponent = std::min(std::max(aValue, 1.0f), 128.0f);
      break;
    default:
      return false;
  }
  return true;
}

void
SpecularLightingSoftware::Prepare()
{
  mPowCache.CacheForExponent(mSpecularExponent);
  mSpecularConstantInt = uint32_t(mSpecularConstant * (1 << 8));
}

uint32_t
SpecularLightingSoftware::LightPixel(const Point3D &aNormal,
                                     const Point3D &aVectorToLight,
                                     uint32_t aColor)
{
  Point3D vectorToEye(0, 0, 1);
  Point3D halfwayVector = Normalized(aVectorToLight + vectorToEye);
  Float dotNH = aNormal.DotProduct(halfwayVector);
  uint16_t dotNHi = uint16_t(dotNH * (dotNH >= 0) * (255 << 7));
  uint32_t specularNHi = mSpecularConstantInt * mPowCache.Pow(dotNHi);

  union {
    uint32_t bgra;
    uint8_t components[4];
  } color = { aColor };
  color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_B] =
    umin(FilterProcessing::FastDivideBy255<uint16_t>(
      specularNHi * color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_B] >> 8), 255U);
  color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_G] =
    umin(FilterProcessing::FastDivideBy255<uint16_t>(
      specularNHi * color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_G] >> 8), 255U);
  color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_R] =
    umin(FilterProcessing::FastDivideBy255<uint16_t>(
      specularNHi * color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_R] >> 8), 255U);

  color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_A] =
    umax(color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_B],
      umax(color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_G],
               color.components[B8G8R8A8_COMPONENT_BYTEOFFSET_R]));
  return color.bgra;
}

} // namespace gfx
} // namespace mozilla
