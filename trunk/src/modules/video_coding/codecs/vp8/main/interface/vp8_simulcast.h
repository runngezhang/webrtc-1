/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/*
 * vp8_simulcast.h
 * WEBRTC VP8 simulcast wrapper interface
 * Creates up to kMaxSimulcastStreams number of VP8 encoders
 * Automatically scale the input frame to the right size for all VP8 encoders
 * Runtime it divides the available bitrate beteween the VP8 Encoders 
 */


#ifndef WEBRTC_MODULES_VIDEO_CODING_CODECS_VP8_SIMULCAST_H_
#define WEBRTC_MODULES_VIDEO_CODING_CODECS_VP8_SIMULCAST_H_

#include "common_video/libyuv/include/scaler.h"

#include "video_codec_interface.h"
#include "vp8.h"

namespace webrtc
{
class VP8SimulcastEncoder : public VideoEncoder
{
public:
  VP8SimulcastEncoder();
  virtual ~VP8SimulcastEncoder();

// Free encoder memory.
//
// Return value                : WEBRTC_VIDEO_CODEC_OK if OK, < 0 otherwise.
  virtual WebRtc_Word32 Release();

// Reset encoder state and prepare for a new call.
//
// Return value                : WEBRTC_VIDEO_CODEC_OK if OK, < 0 otherwise.
//                               <0 - Errors:
//                                 WEBRTC_VIDEO_CODEC_ERR_PARAMETER
//                                 WEBRTC_VIDEO_CODEC_ERROR
  virtual WebRtc_Word32 Reset();

// Initialize the encoder with the information from the codecSettings
//
// Input:
//          - codecSettings     : Codec settings
//          - numberOfCores     : Number of cores available for the encoder
//          - maxPayloadSize    : The maximum size each payload is allowed
//                                to have. Usually MTU - overhead.
//
// Return value                 : Set bit rate if OK
//                                <0 - Errors:
//                                  WEBRTC_VIDEO_CODEC_ERR_PARAMETER
//                                  WEBRTC_VIDEO_CODEC_ERR_SIZE
//                                  WEBRTC_VIDEO_CODEC_LEVEL_EXCEEDED
//                                  WEBRTC_VIDEO_CODEC_MEMORY
//                                  WEBRTC_VIDEO_CODEC_ERROR
  virtual WebRtc_Word32 InitEncode(const VideoCodec* codecSettings,
                                   WebRtc_Word32 numberOfCores,
                                   WebRtc_UWord32 maxPayloadSize);

// Encode an I420 image (as a part of a video stream). The encoded image
// will be returned to the user through the encode complete callback. 
// It can encode multiple streams based on its configuration but not more than
// kMaxSimulcastStreams. 
//
// Input:
//          - inputImage        : Image to be encoded
//          - frameTypes        : Frame type to be generated by the encoder
//                                pointer to first frame type in array the
//                                caller is responsible for the length of the
//                                array to be no shorter than number of encoders
//                                configured.
//
// Return value                 : WEBRTC_VIDEO_CODEC_OK if OK
//                                <0 - Errors:
//                                  WEBRTC_VIDEO_CODEC_ERR_PARAMETER
//                                  WEBRTC_VIDEO_CODEC_MEMORY
//                                  WEBRTC_VIDEO_CODEC_ERROR
//                                  WEBRTC_VIDEO_CODEC_TIMEOUT
  virtual WebRtc_Word32 Encode(const RawImage& inputImage,
                               const CodecSpecificInfo* codecSpecificInfo,
                               const VideoFrameType* frameTypes);

// Register an encode complete callback object.
//
// Input:
//          - callback         : Callback object which handles encoded images.
//
// Return value                : WEBRTC_VIDEO_CODEC_OK if OK, < 0 otherwise.
  virtual WebRtc_Word32 RegisterEncodeCompleteCallback(
      EncodedImageCallback* callback);

// Inform the encoder of the new packet loss rate and round-trip time of the
// network
//
//          - packetLoss       : Fraction lost
//                               (loss rate in percent = 100 * packetLoss / 255)
//          - rtt              : Round-trip time in milliseconds
// Return value                : WEBRTC_VIDEO_CODEC_OK if OK
//                               <0 - Errors:
//                                  WEBRTC_VIDEO_CODEC_ERROR
//
  virtual WebRtc_Word32 SetChannelParameters(WebRtc_UWord32 packetLoss,
                                             int rtt);

// Inform the encoder about the new target bit rate.
//
//          - newBitRate       : New target bit rate
//          - frameRate        : The target frame rate
//
// Return value                : WEBRTC_VIDEO_CODEC_OK if OK, < 0 otherwise.
  virtual WebRtc_Word32 SetRates(WebRtc_UWord32 newBitRateKbit,
                                 WebRtc_UWord32 frameRate);

// Get version number for the codec.
//
// Input:
//      - version       : Pointer to allocated char buffer.
//      - buflen        : Length of provided char buffer.
//
// Output:
//      - version       : Version number string written to char buffer.
//
// Return value         : >0 - Length of written string.
//                        <0 - WEBRTC_VIDEO_CODEC_ERR_SIZE
  virtual WebRtc_Word32 Version(WebRtc_Word8 *version,
                                WebRtc_Word32 length) const;
  static WebRtc_Word32  VersionStatic(WebRtc_Word8 *version,
                                      WebRtc_Word32 length);

private:
  VP8Encoder* encoder_[kMaxSimulcastStreams];
  bool encode_stream_[kMaxSimulcastStreams];
  VideoFrameType frame_type_[kMaxSimulcastStreams];
  Scaler* scaler_[kMaxSimulcastStreams];
  RawImage video_frame_[kMaxSimulcastStreams];
  VideoCodec video_codec_;
};// end of VP8SimulcastEncoder class
} // namespace webrtc
#endif // WEBRTC_MODULES_VIDEO_CODING_CODECS_VP8_SIMULCAST_H_

