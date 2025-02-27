/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_VIDEO_ENGINE_VIE_CAPTURE_IMPL_H_
#define WEBRTC_VIDEO_ENGINE_VIE_CAPTURE_IMPL_H_

#include "typedefs.h"
#include "video_engine/main/interface/vie_capture.h"
#include "video_engine/vie_defines.h"
#include "video_engine/vie_ref_count.h"
#include "video_engine/vie_shared_data.h"

namespace webrtc {

class ViECaptureImpl
    : public virtual ViESharedData,
      public ViECapture,
      public ViERefCount {
 public:
  // Implements ViECapture.
  virtual int Release();
  virtual int NumberOfCaptureDevices();
  virtual int GetCaptureDevice(unsigned int list_number, char* device_nameUTF8,
                               const unsigned int device_nameUTF8Length,
                               char* unique_idUTF8,
                               const unsigned int unique_idUTF8Length);
  virtual int AllocateCaptureDevice(const char* unique_idUTF8,
                                    const unsigned int unique_idUTF8Length,
                                    int& capture_id);
  virtual int AllocateCaptureDevice(VideoCaptureModule& capture_module,
                                    int& capture_id);
  virtual int AllocateExternalCaptureDevice(
      int& capture_id, ViEExternalCapture *&external_capture);
  virtual int ReleaseCaptureDevice(const int capture_id);

  virtual int ConnectCaptureDevice(const int capture_id,
                                   const int video_channel);
  virtual int DisconnectCaptureDevice(const int video_channel);
  virtual int StartCapture(
      const int capture_id,
      const CaptureCapability capture_capability = CaptureCapability());
  virtual int StopCapture(const int capture_id);
  virtual int SetRotateCapturedFrames(const int capture_id,
                                      const RotateCapturedFrame rotation);
  virtual int SetCaptureDelay(const int capture_id,
                              const unsigned int capture_delay_ms);
  virtual int NumberOfCapabilities(const char* unique_idUTF8,
                                   const unsigned int unique_idUTF8Length);
  virtual int GetCaptureCapability(const char* unique_idUTF8,
                                   const unsigned int unique_idUTF8Length,
                                   const unsigned int capability_number,
                                   CaptureCapability& capability);
  virtual int ShowCaptureSettingsDialogBox(
    const char* unique_idUTF8, const unsigned int unique_idUTF8Length,
    const char* dialog_title, void* parent_window = NULL,
    const unsigned int x = 200, const unsigned int y = 200);
  virtual int GetOrientation(const char* unique_idUTF8,
                             RotateCapturedFrame& orientation);
  virtual int EnableBrightnessAlarm(const int capture_id, const bool enable);
  virtual int RegisterObserver(const int capture_id,
                               ViECaptureObserver& observer);
  virtual int DeregisterObserver(const int capture_id);

 protected:
  ViECaptureImpl();
  virtual ~ViECaptureImpl();
};

}  // namespace webrtc

#endif  // WEBRTC_VIDEO_ENGINE_VIE_CAPTURE_IMPL_H_
