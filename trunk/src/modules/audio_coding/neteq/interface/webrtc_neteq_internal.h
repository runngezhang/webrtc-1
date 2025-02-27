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
 * This file contains the internal API functions.
 */

#include "typedefs.h"

#ifndef WEBRTC_NETEQ_INTERNAL_H
#define WEBRTC_NETEQ_INTERNAL_H

#ifdef __cplusplus 
extern "C"
{
#endif

typedef struct
{
    WebRtc_UWord8 payloadType;
    WebRtc_UWord16 sequenceNumber;
    WebRtc_UWord32 timeStamp;
    WebRtc_UWord32 SSRC;
    WebRtc_UWord8 markerBit;
} WebRtcNetEQ_RTPInfo;

/****************************************************************************
 * WebRtcNetEQ_RecInRTPStruct(...)
 *
 * Alternative RecIn function, used when the RTP data has already been
 * parsed into an RTP info struct (WebRtcNetEQ_RTPInfo).
 *
 * Input:
 *		- inst	            : NetEQ instance
 *		- rtpInfo		    : Pointer to RTP info
 *		- payloadPtr        : Pointer to the RTP payload (first byte after header)
 *      - payloadLenBytes   : Length (in bytes) of the payload in payloadPtr
 *      - timeRec           : Receive time (in timestamps of the used codec)
 *
 * Return value			    :  0 - Ok
 *                            -1 - Error
 */
int WebRtcNetEQ_RecInRTPStruct(void *inst, WebRtcNetEQ_RTPInfo *rtpInfo,
                               const WebRtc_UWord8 *payloadPtr, WebRtc_Word16 payloadLenBytes,
                               WebRtc_UWord32 timeRec);

/****************************************************************************
 * WebRtcNetEQ_GetMasterSlaveInfoSize(...)
 *
 * Get size in bytes for master/slave struct msInfo used in 
 * WebRtcNetEQ_RecOutMasterSlave.
 *
 * Return value			    :  Struct size in bytes
 * 
 */

int WebRtcNetEQ_GetMasterSlaveInfoSize();

/****************************************************************************
 * WebRtcNetEQ_RecOutMasterSlave(...)
 *
 * RecOut function for running several NetEQ instances in master/slave mode.
 * One master can be used to control several slaves. 
 * The MasterSlaveInfo struct must be allocated outside NetEQ.
 * Use function WebRtcNetEQ_GetMasterSlaveInfoSize to get the size needed.
 *
 * Input:
 *      - inst          : NetEQ instance
 *      - isMaster      : Non-zero indicates that this is the master channel
 *      - msInfo        : (slave only) Information from master
 *
 * Output:
 *		- inst	        : Updated NetEQ instance
 *      - pw16_outData  : Pointer to vector where output should be written
 *      - pw16_len      : Pointer to variable where output length is returned
 *      - msInfo        : (master only) Information to slave(s)
 *
 * Return value			:  0 - Ok
 *						  -1 - Error
 */

int WebRtcNetEQ_RecOutMasterSlave(void *inst, WebRtc_Word16 *pw16_outData,
                                  WebRtc_Word16 *pw16_len, void *msInfo,
                                  WebRtc_Word16 isMaster);

typedef struct
{
    WebRtc_UWord16 currentBufferSize; /* current jitter buffer size in ms */
    WebRtc_UWord16 preferredBufferSize; /* preferred (optimal) buffer size in ms */
    WebRtc_UWord16 currentPacketLossRate; /* loss rate (network + late) (in Q14) */
    WebRtc_UWord16 currentDiscardRate; /* late loss rate (in Q14) */
    WebRtc_UWord16 currentExpandRate; /* fraction (of original stream) of synthesized speech
                                       * inserted through expansion (in Q14) */
    WebRtc_UWord16 currentPreemptiveRate; /* fraction of synthesized speech inserted through
                                           * pre-emptive expansion (in Q14) */
    WebRtc_UWord16 currentAccelerateRate; /* fraction of data removed through acceleration
                                           * (in Q14) */
} WebRtcNetEQ_NetworkStatistics;

/*
 * Get the "in-call" statistics from NetEQ.
 * The statistics are reset after the query.
 */
int WebRtcNetEQ_GetNetworkStatistics(void *inst, WebRtcNetEQ_NetworkStatistics *stats);

/***********************************************/
/* Functions for post-decode VAD functionality */
/***********************************************/

/* NetEQ must be compiled with the flag NETEQ_VAD enabled for these functions to work. */

/*
 * VAD function pointer types
 *
 * These function pointers match the definitions of webrtc VAD functions WebRtcVad_Init,
 * WebRtcVad_set_mode and WebRtcVad_Process, respectively, all found in webrtc_vad.h.
 */
typedef WebRtc_Word16 (*WebRtcNetEQ_VADInitFunction)(void *VAD_inst);
typedef WebRtc_Word16 (*WebRtcNetEQ_VADSetmodeFunction)(void *VAD_inst, WebRtc_Word16 mode);
typedef WebRtc_Word16 (*WebRtcNetEQ_VADFunction)(void *VAD_inst, WebRtc_Word16 fs,
                                                 WebRtc_Word16 *frame, WebRtc_Word16 frameLen);

/****************************************************************************
 * WebRtcNetEQ_SetVADInstance(...)
 *
 * Provide a pointer to an allocated VAD instance. If function is never 
 * called or it is called with NULL pointer as VAD_inst, the post-decode
 * VAD functionality is disabled. Also provide pointers to init, setmode
 * and VAD functions. These are typically pointers to WebRtcVad_Init,
 * WebRtcVad_set_mode and WebRtcVad_Process, respectively, all found in the
 * interface file webrtc_vad.h.
 *
 * Input:
 *      - NetEQ_inst        : NetEQ instance
 *		- VADinst		    : VAD instance
 *		- initFunction	    : Pointer to VAD init function
 *		- setmodeFunction   : Pointer to VAD setmode function
 *		- VADfunction	    : Pointer to VAD function
 *
 * Output:
 *		- NetEQ_inst	    : Updated NetEQ instance
 *
 * Return value			    :  0 - Ok
 *						      -1 - Error
 */

int WebRtcNetEQ_SetVADInstance(void *NetEQ_inst, void *VAD_inst,
                               WebRtcNetEQ_VADInitFunction initFunction,
                               WebRtcNetEQ_VADSetmodeFunction setmodeFunction,
                               WebRtcNetEQ_VADFunction VADFunction);

/****************************************************************************
 * WebRtcNetEQ_SetVADMode(...)
 *
 * Pass an aggressiveness mode parameter to the post-decode VAD instance.
 * If this function is never called, mode 0 (quality mode) is used as default.
 *
 * Input:
 *      - inst          : NetEQ instance
 *		- mode  		: mode parameter (same range as WebRtc VAD mode)
 *
 * Output:
 *		- inst	        : Updated NetEQ instance
 *
 * Return value			:  0 - Ok
 *						  -1 - Error
 */

int WebRtcNetEQ_SetVADMode(void *NetEQ_inst, WebRtc_Word16 mode);

/****************************************************************************
 * WebRtcNetEQ_RecOutNoDecode(...)
 *
 * Special RecOut that does not do any decoding.
 *
 * Input:
 *      - inst          : NetEQ instance
 *
 * Output:
 *		- inst	        : Updated NetEQ instance
 *      - pw16_outData  : Pointer to vector where output should be written
 *      - pw16_len      : Pointer to variable where output length is returned
 *
 * Return value			:  0 - Ok
 *						  -1 - Error
 */

int WebRtcNetEQ_RecOutNoDecode(void *inst, WebRtc_Word16 *pw16_outData,
                               WebRtc_Word16 *pw16_len);

/****************************************************************************
 * WebRtcNetEQ_FlushBuffers(...)
 *
 * Flush packet and speech buffers. Does not reset codec database or 
 * jitter statistics.
 *
 * Input:
 *      - inst          : NetEQ instance
 *
 * Output:
 *		- inst	        : Updated NetEQ instance
 *
 * Return value			:  0 - Ok
 *						  -1 - Error
 */

int WebRtcNetEQ_FlushBuffers(void *inst);

#ifdef __cplusplus
}
#endif

#endif
