/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_AUDIO_PROCESSING_MAIN_SOURCE_ECHO_CANCELLATION_IMPL_H_
#define WEBRTC_MODULES_AUDIO_PROCESSING_MAIN_SOURCE_ECHO_CANCELLATION_IMPL_H_

#include "audio_processing.h"
#include "processing_component.h"

namespace webrtc {
class AudioProcessingImpl;
class AudioBuffer;

class EchoCancellationImpl : public EchoCancellation,
                             public ProcessingComponent {
 public:
  explicit EchoCancellationImpl(const AudioProcessingImpl* apm);
  virtual ~EchoCancellationImpl();

  int ProcessRenderAudio(const AudioBuffer* audio);
  int ProcessCaptureAudio(AudioBuffer* audio);

#if (DITECH_VERSION==2)
  virtual void set_processing_discontinuity(bool);		

#endif


  // EchoCancellation implementation.
  virtual bool is_enabled() const;
  virtual int device_sample_rate_hz() const;
  virtual int stream_drift_samples() const;

  // ProcessingComponent implementation.
  virtual int Initialize();
  virtual int get_version(char* version, int version_len_bytes) const;

#if (DITECH_VERSION==2)
  virtual int bufferFarEnd_nsinha(const AudioBuffer* audio);		
#endif

 private:
  // EchoCancellation implementation.
  virtual int Enable(bool enable);
  virtual int enable_drift_compensation(bool enable);
  virtual bool is_drift_compensation_enabled() const;
  virtual int set_device_sample_rate_hz(int rate);
  virtual int set_stream_drift_samples(int drift);
  virtual int set_suppression_level(SuppressionLevel level);
  virtual SuppressionLevel suppression_level() const;
  virtual int enable_metrics(bool enable);
  virtual bool are_metrics_enabled() const;
  virtual bool stream_has_echo() const;
  virtual int GetMetrics(Metrics* metrics);
  virtual int enable_delay_logging(bool enable);
  virtual bool is_delay_logging_enabled() const;
  virtual int GetDelayMetrics(int* median, int* std);

  // ProcessingComponent implementation.
  virtual void* CreateHandle() const;
  virtual int InitializeHandle(void* handle) const;
  virtual int ConfigureHandle(void* handle) const;
  virtual int DestroyHandle(void* handle) const;
  virtual int num_handles_required() const;
  virtual int GetHandleError(void* handle) const;

  const AudioProcessingImpl* apm_;
  bool drift_compensation_enabled_;
  bool metrics_enabled_;
  SuppressionLevel suppression_level_;
  int device_sample_rate_hz_;
  int stream_drift_samples_;
  bool was_stream_drift_set_;
  bool stream_has_echo_;
  bool delay_logging_enabled_;
};
}  // namespace webrtc

#endif  // WEBRTC_MODULES_AUDIO_PROCESSING_MAIN_SOURCE_ECHO_CANCELLATION_IMPL_H_
