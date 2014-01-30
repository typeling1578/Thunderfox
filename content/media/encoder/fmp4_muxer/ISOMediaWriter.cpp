/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ISOMediaWriter.h"
#include "ISOControl.h"
#include "ISOMediaBoxes.h"
#include "ISOTrackMetadata.h"
#include "nsThreadUtils.h"
#include "MediaEncoder.h"

#undef LOG
#ifdef MOZ_WIDGET_GONK
#include <android/log.h>
#define LOG(args...) __android_log_print(ANDROID_LOG_INFO, "MediaEncoder", ## args);
#else
#define LOG(args, ...)
#endif

namespace mozilla {

const static uint32_t FRAG_DURATION = 2000000;    // microsecond per unit

ISOMediaWriter::ISOMediaWriter(uint32_t aType)
  : ContainerWriter()
  , mState(MUXING_HEAD)
  , mBlobReady(false)
  , mType(0)
{
  if (aType & CREATE_AUDIO_TRACK) {
    mType |= Audio_Track;
  }
  if (aType & CREATE_VIDEO_TRACK) {
    mType |= Video_Track;
  }
  mControl = new ISOControl();
  MOZ_COUNT_CTOR(ISOMediaWriter);
}

ISOMediaWriter::~ISOMediaWriter()
{
  MOZ_COUNT_DTOR(ISOMediaWriter);
}

nsresult
ISOMediaWriter::RunState()
{
  nsresult rv;
  switch (mState) {
    case MUXING_HEAD:
    {
      rv = mControl->GenerateFtyp();
      NS_ENSURE_SUCCESS(rv, rv);
      rv = mControl->GenerateMoov();
      NS_ENSURE_SUCCESS(rv, rv);
      mState = MUXING_FRAG;
      break;
    }
    case MUXING_FRAG:
    {
      rv = mControl->GenerateMoof(mType);
      NS_ENSURE_SUCCESS(rv, rv);

      bool EOS;
      if (ReadyToRunState(EOS) && EOS) {
        mState = MUXING_DONE;
      }
      break;
    }
    case MUXING_DONE:
    {
      break;
    }
  }
  mBlobReady = true;
  return NS_OK;
}

nsresult
ISOMediaWriter::WriteEncodedTrack(const EncodedFrameContainer& aData,
                                  uint32_t aFlags)
{
  // Muxing complete, it doesn't allowed to reentry again.
  if (mState == MUXING_DONE) {
    MOZ_ASSERT(false);
    return NS_ERROR_FAILURE;
  }

  FragmentBuffer* frag = nullptr;
  uint32_t len = aData.GetEncodedFrames().Length();

  if (!len) {
    // no frame? why bother to WriteEncodedTrack
    return NS_OK;
  }
  for (uint32_t i = 0; i < len; i++) {
    nsRefPtr<EncodedFrame> frame(aData.GetEncodedFrames()[i]);
    EncodedFrame::FrameType type = frame->GetFrameType();
    if (type == EncodedFrame::AUDIO_FRAME ||
        type == EncodedFrame::AAC_CSD) {
      frag = mAudioFragmentBuffer;
    } else if (type == EncodedFrame::I_FRAME ||
               type == EncodedFrame::P_FRAME ||
               type == EncodedFrame::B_FRAME ||
               type == EncodedFrame::AVC_CSD) {
      frag = mVideoFragmentBuffer;
    } else {
      MOZ_ASSERT(0);
      return NS_ERROR_FAILURE;
    }

    frag->AddFrame(frame);
  }

  // Encoder should send CSD (codec specific data) frame before sending the
  // audio/video frames. When CSD data is ready, it is sufficient to generate a
  // moov data. If encoder doesn't send CSD yet, muxer needs to wait before
  // generating anything.
  if (mType & Audio_Track && !mAudioFragmentBuffer->HasCSD()) {
    return NS_OK;
  }
  if (mType & Video_Track && !mVideoFragmentBuffer->HasCSD()) {
    return NS_OK;
  }

  // Only one FrameType in EncodedFrameContainer so it doesn't need to be
  // inside the for-loop.
  if (frag && (aFlags & END_OF_STREAM)) {
    frag->SetEndOfStream();
  }

  nsresult rv;
  bool EOS;
  if (ReadyToRunState(EOS)) {
    // TODO:
    // The MediaEncoder doesn't use nsRunnable, so thread will be
    // stocked on that part and the new added nsRunnable won't get to run
    // before MediaEncoder completing. Before MediaEncoder change, it needs
    // to call RunState directly.
    // https://bugzilla.mozilla.org/show_bug.cgi?id=950429
    rv = RunState();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

bool
ISOMediaWriter::ReadyToRunState(bool& aEOS)
{
  aEOS = false;
  bool bReadyToMux = true;
  if ((mType & Audio_Track) && (mType & Video_Track)) {
    if (!mAudioFragmentBuffer->HasEnoughData()) {
      bReadyToMux = false;
    }
    if (!mVideoFragmentBuffer->HasEnoughData()) {
      bReadyToMux = false;
    }

    if (mAudioFragmentBuffer->EOS() && mVideoFragmentBuffer->EOS()) {
      aEOS = true;
      bReadyToMux = true;
    }
  } else if (mType == Audio_Track) {
    if (!mAudioFragmentBuffer->HasEnoughData()) {
      bReadyToMux = false;
    }
    if (mAudioFragmentBuffer->EOS()) {
      aEOS = true;
      bReadyToMux = true;
    }
  } else if (mType == Video_Track) {
    if (!mVideoFragmentBuffer->HasEnoughData()) {
      bReadyToMux = false;
    }
    if (mVideoFragmentBuffer->EOS()) {
      aEOS = true;
      bReadyToMux = true;
    }
  }

  return bReadyToMux;
}

nsresult
ISOMediaWriter::GetContainerData(nsTArray<nsTArray<uint8_t>>* aOutputBufs,
                                 uint32_t aFlags)
{
  if (mBlobReady) {
    if (mState == MUXING_DONE) {
      mIsWritingComplete = true;
    }
    mBlobReady = false;
    aOutputBufs->AppendElement();
    return mControl->GetBuf(aOutputBufs->LastElement());
  }
  return NS_OK;
}

nsresult
ISOMediaWriter::SetMetadata(TrackMetadataBase* aMetadata)
{
  if (aMetadata->GetKind() == TrackMetadataBase::METADATA_AAC ) {
    mControl->SetMetadata(aMetadata);
    mAudioFragmentBuffer = new FragmentBuffer(Audio_Track,
                                              FRAG_DURATION,
                                              aMetadata);
    mControl->SetFragment(mAudioFragmentBuffer);
    return NS_OK;
  }
  if (aMetadata->GetKind() == TrackMetadataBase::METADATA_AVC) {
    mControl->SetMetadata(aMetadata);
    mVideoFragmentBuffer = new FragmentBuffer(Video_Track,
                                              FRAG_DURATION,
                                              aMetadata);
    mControl->SetFragment(mVideoFragmentBuffer);
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

}  // namespace mozilla
