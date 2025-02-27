/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_VIDEO_CODING_DECODING_STATE_H_
#define WEBRTC_MODULES_VIDEO_CODING_DECODING_STATE_H_

#include "typedefs.h"

namespace webrtc {

// Forward declarations
class VCMFrameBuffer;
class VCMPacket;

class VCMDecodingState {
 public:
  VCMDecodingState();
  ~VCMDecodingState();
  // Check for old frame
  bool IsOldFrame(const VCMFrameBuffer* frame) const;
  // Check for old packet
  bool IsOldPacket(const VCMPacket* packet) const;
  // Check for frame continuity based on current decoded state. Use best method
  // possible, i.e. temporal info, picture ID or sequence number.
  bool ContinuousFrame(const VCMFrameBuffer* frame) const;
  void SetState(const VCMFrameBuffer* frame);
  // Set the decoding state one frame back.
  void SetStateOneBack(const VCMFrameBuffer* frame);
  // Update the sequence number if the timestamp matches current state and the
  // packet is empty.
  void UpdateEmptyPacket(const VCMPacket* packet);
  void SetSeqNum(uint16_t new_seq_num);
  void Reset();
  uint32_t time_stamp() const;
  uint16_t sequence_num() const;
  // Return true if at initial state.
  bool init() const;
  // Return true when sync is on - decode all layers.
  bool full_sync() const;

 private:
  void UpdateSyncState(const VCMFrameBuffer* frame);
  // Designated continuity functions
  bool ContinuousPictureId(int picture_id) const;
  bool ContinuousSeqNum(uint16_t seq_num) const;
  bool ContinuousLayer(int temporal_id, int tl0_pic_id) const;

  // Keep state of last decoded frame.
  // TODO(mikhal/stefan): create designated classes to handle these types.
  uint16_t    sequence_num_;
  uint32_t    time_stamp_;
  int         picture_id_;
  int         temporal_id_;
  int         tl0_pic_id_;
  bool        full_sync_;  // Sync flag when temporal layers are used.
  bool        init_;
};

}  // namespace webrtc

#endif  // WEBRTC_MODULES_VIDEO_CODING_DECODING_STATE_H_
