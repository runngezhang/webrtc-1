/*
 * libjingle
 * Copyright 2004--2007, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "talk/session/phone/channel.h"

#include "talk/base/buffer.h"
#include "talk/base/byteorder.h"
#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/p2p/base/transportchannel.h"
#include "talk/session/phone/channelmanager.h"
#include "talk/session/phone/mediasessionclient.h"
#include "talk/session/phone/rtcpmuxfilter.h"
#include "talk/session/phone/rtputils.h"

namespace cricket {

struct PacketMessageData : public talk_base::MessageData {
  talk_base::Buffer packet;
};

struct VoiceChannelErrorMessageData : public talk_base::MessageData {
  VoiceChannelErrorMessageData(uint32 in_ssrc,
                               VoiceMediaChannel::Error in_error)
      : ssrc(in_ssrc),
        error(in_error) {}
  uint32 ssrc;
  VoiceMediaChannel::Error error;
};

struct VideoChannelErrorMessageData : public talk_base::MessageData {
  VideoChannelErrorMessageData(uint32 in_ssrc,
                               VideoMediaChannel::Error in_error)
      : ssrc(in_ssrc),
        error(in_error) {}
  uint32 ssrc;
  VideoMediaChannel::Error error;
};

static const char* PacketType(bool rtcp) {
  return (!rtcp) ? "RTP" : "RTCP";
}

static bool ValidPacket(bool rtcp, const talk_base::Buffer* packet) {
  // Check the packet size. We could check the header too if needed.
  return (packet &&
      packet->length() >= (!rtcp ? kMinRtpPacketLen : kMinRtcpPacketLen) &&
      packet->length() <= kMaxRtpPacketLen);
}

BaseChannel::BaseChannel(talk_base::Thread* thread,
                         MediaEngineInterface* media_engine,
                         MediaChannel* media_channel, BaseSession* session,
                         const std::string& content_name, bool rtcp)
    : worker_thread_(thread),
      media_engine_(media_engine),
      session_(session),
      media_channel_(media_channel),
      content_name_(content_name),
      rtcp_(rtcp),
      transport_channel_(NULL),
      rtcp_transport_channel_(NULL),
      enabled_(false),
      writable_(false),
      was_ever_writable_(false),
      has_local_content_(false),
      has_remote_content_(false),
      muted_(false) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  LOG(LS_INFO) << "Created channel";
}

BaseChannel::~BaseChannel() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  StopConnectionMonitor();
  FlushRtcpMessages();  // Send any outstanding RTCP packets.
  Clear();  // eats any outstanding messages or packets
  // We must destroy the media channel before the transport channel, otherwise
  // the media channel may try to send on the dead transport channel. NULLing
  // is not an effective strategy since the sends will come on another thread.
  delete media_channel_;
  set_rtcp_transport_channel(NULL);
  if (transport_channel_ != NULL)
    session_->DestroyChannel(content_name_, transport_channel_->name());
  LOG(LS_INFO) << "Destroyed channel";
}

bool BaseChannel::Init(TransportChannel* transport_channel,
                       TransportChannel* rtcp_transport_channel) {
  if (transport_channel == NULL) {
    return false;
  }
  if (rtcp() && rtcp_transport_channel == NULL) {
    return false;
  }
  transport_channel_ = transport_channel;
  media_channel_->SetInterface(this);
  transport_channel_->SignalWritableState.connect(
      this, &BaseChannel::OnWritableState);
  transport_channel_->SignalReadPacket.connect(
      this, &BaseChannel::OnChannelRead);

  session_->SignalState.connect(this, &BaseChannel::OnSessionState);
  session_->SignalRemoteDescriptionUpdate.connect(this,
      &BaseChannel::OnRemoteDescriptionUpdate);

  OnSessionState(session(), session()->state());
  set_rtcp_transport_channel(rtcp_transport_channel);
  return true;
}

// Can be called from thread other than worker thread
bool BaseChannel::Enable(bool enable) {
  Send(enable ? MSG_ENABLE : MSG_DISABLE);
  return true;
}

// Can be called from thread other than worker thread
bool BaseChannel::Mute(bool mute) {
  Clear(MSG_UNMUTE);  // Clear any penging auto-unmutes.
  Send(mute ? MSG_MUTE : MSG_UNMUTE);
  return true;
}

bool BaseChannel::RemoveStream(uint32 ssrc) {
  StreamMessageData data(ssrc, 0);
  Send(MSG_REMOVESTREAM, &data);
  ssrc_filter()->RemoveStream(ssrc);
  return true;
}

bool BaseChannel::SetRtcpCName(const std::string& cname) {
  SetRtcpCNameData data(cname);
  Send(MSG_SETRTCPCNAME, &data);
  return data.result;
}

bool BaseChannel::SetLocalContent(const MediaContentDescription* content,
                                  ContentAction action) {
  SetContentData data(content, action);
  Send(MSG_SETLOCALCONTENT, &data);
  return data.result;
}

bool BaseChannel::SetRemoteContent(const MediaContentDescription* content,
                                   ContentAction action) {
  SetContentData data(content, action);
  Send(MSG_SETREMOTECONTENT, &data);
  return data.result;
}

bool BaseChannel::SetMaxSendBandwidth(int max_bandwidth) {
  SetBandwidthData data(max_bandwidth);
  Send(MSG_SETMAXSENDBANDWIDTH, &data);
  return data.result;
}

void BaseChannel::StartConnectionMonitor(int cms) {
  socket_monitor_.reset(new SocketMonitor(transport_channel_,
                                          worker_thread(),
                                          talk_base::Thread::Current()));
  socket_monitor_->SignalUpdate.connect(
      this, &BaseChannel::OnConnectionMonitorUpdate);
  socket_monitor_->Start(cms);
}

void BaseChannel::StopConnectionMonitor() {
  if (socket_monitor_.get()) {
    socket_monitor_->Stop();
    socket_monitor_.reset();
  }
}

void BaseChannel::set_rtcp_transport_channel(TransportChannel* channel) {
  if (rtcp_transport_channel_ != channel) {
    if (rtcp_transport_channel_) {
      session_->DestroyChannel(content_name_, rtcp_transport_channel_->name());
    }
    rtcp_transport_channel_ = channel;
    if (rtcp_transport_channel_) {
      rtcp_transport_channel_->SignalWritableState.connect(
          this, &BaseChannel::OnWritableState);
      rtcp_transport_channel_->SignalReadPacket.connect(
          this, &BaseChannel::OnChannelRead);
    }
  }
}

bool BaseChannel::SendPacket(talk_base::Buffer* packet) {
  return SendPacket(false, packet);
}

bool BaseChannel::SendRtcp(talk_base::Buffer* packet) {
  return SendPacket(true, packet);
}

int BaseChannel::SetOption(SocketType type, talk_base::Socket::Option opt,
                           int value) {
  switch (type) {
    case ST_RTP: return transport_channel_->SetOption(opt, value);
    case ST_RTCP: return rtcp_transport_channel_->SetOption(opt, value);
    default: return -1;
  }
}

void BaseChannel::OnWritableState(TransportChannel* channel) {
  ASSERT(channel == transport_channel_ || channel == rtcp_transport_channel_);
  if (transport_channel_->writable()
      && (!rtcp_transport_channel_ || rtcp_transport_channel_->writable())) {
    ChannelWritable_w();
  } else {
    ChannelNotWritable_w();
  }
}

void BaseChannel::OnChannelRead(TransportChannel* channel,
                                const char* data, size_t len) {
  // OnChannelRead gets called from P2PSocket; now pass data to MediaEngine
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // When using RTCP multiplexing we might get RTCP packets on the RTP
  // transport. We feed RTP traffic into the demuxer to determine if it is RTCP.
  bool rtcp = PacketIsRtcp(channel, data, len);
  talk_base::Buffer packet(data, len);
  HandlePacket(rtcp, &packet);
}

bool BaseChannel::PacketIsRtcp(const TransportChannel* channel,
                               const char* data, size_t len) {
  return (channel == rtcp_transport_channel_ ||
          rtcp_mux_filter_.DemuxRtcp(data, len));
}

bool BaseChannel::SendPacket(bool rtcp, talk_base::Buffer* packet) {
  // Ensure we have a path capable of sending packets.
  if (!writable_) {
    return false;
  }

  // SendPacket gets called from MediaEngine, typically on an encoder thread.
  // If the thread is not our worker thread, we will post to our worker
  // so that the real work happens on our worker. This avoids us having to
  // synchronize access to all the pieces of the send path, including
  // SRTP and the inner workings of the transport channels.
  // The only downside is that we can't return a proper failure code if
  // needed. Since UDP is unreliable anyway, this should be a non-issue.
  if (talk_base::Thread::Current() != worker_thread_) {
    // Avoid a copy by transferring the ownership of the packet data.
    int message_id = (!rtcp) ? MSG_RTPPACKET : MSG_RTCPPACKET;
    PacketMessageData* data = new PacketMessageData;
    packet->TransferTo(&data->packet);
    worker_thread_->Post(this, message_id, data);
    return true;
  }

  // Now that we are on the correct thread, ensure we have a place to send this
  // packet before doing anything. (We might get RTCP packets that we don't
  // intend to send.) If we've negotiated RTCP mux, send RTCP over the RTP
  // transport.
  TransportChannel* channel = (!rtcp || rtcp_mux_filter_.IsActive()) ?
      transport_channel_ : rtcp_transport_channel_;
  if (!channel || !channel->writable()) {
    return false;
  }

  // Protect ourselves against crazy data.
  if (!ValidPacket(rtcp, packet)) {
    LOG(LS_ERROR) << "Dropping outgoing " << content_name_ << " "
                  << PacketType(rtcp) << " packet: wrong size="
                  << packet->length();
    return false;
  }

  // Protect if needed.
  if (srtp_filter_.IsActive()) {
    bool res;
    char* data = packet->data();
    int len = packet->length();
    if (!rtcp) {
      res = srtp_filter_.ProtectRtp(data, len, packet->capacity(), &len);
      if (!res) {
        int seq_num = -1;
        uint32 ssrc = 0;
        GetRtpSeqNum(data, len, &seq_num);
        GetRtpSsrc(data, len, &ssrc);
        LOG(LS_ERROR) << "Failed to protect " << content_name_
                      << " RTP packet: size=" << len
                      << ", seqnum=" << seq_num << ", SSRC=" << ssrc;
        return false;
      }
    } else {
      res = srtp_filter_.ProtectRtcp(data, len, packet->capacity(), &len);
      if (!res) {
        int type = -1;
        GetRtcpType(data, len, &type);
        LOG(LS_ERROR) << "Failed to protect " << content_name_
                      << " RTCP packet: size=" << len << ", type=" << type;
        return false;
      }
    }

    // Update the length of the packet now that we've added the auth tag.
    packet->SetLength(len);
  }

  // Signal to the media sink after protecting the packet. TODO:
  // Separate APIs to record unprotected media and protected header.
  {
    talk_base::CritScope cs(&signal_send_packet_cs_);
    SignalSendPacket(packet->data(), packet->length(), rtcp);
  }

  // Bon voyage.
  return (channel->SendPacket(packet->data(), packet->length())
      == static_cast<int>(packet->length()));
}

void BaseChannel::HandlePacket(bool rtcp, talk_base::Buffer* packet) {
  // Protect ourselvs against crazy data.
  if (!ValidPacket(rtcp, packet)) {
    LOG(LS_ERROR) << "Dropping incoming " << content_name_ << " "
                  << PacketType(rtcp) << " packet: wrong size="
                  << packet->length();
    return;
  }

  // If this channel is suppose to handle RTP data, that is determined by
  // checking against ssrc filter. This is necessary to do it here to avoid
  // double decryption.
  if (ssrc_filter_.IsActive() &&
      !ssrc_filter_.DemuxPacket(packet->data(), packet->length(), rtcp)) {
    return;
  }

  // Signal to the media sink before unprotecting the packet. TODO:
  // Separate APIs to record unprotected media and protected header.
  {
    talk_base::CritScope cs(&signal_recv_packet_cs_);
    SignalRecvPacket(packet->data(), packet->length(), rtcp);
  }

  // Unprotect the packet, if needed.
  if (srtp_filter_.IsActive()) {
    char* data = packet->data();
    int len = packet->length();
    bool res;
    if (!rtcp) {
      res = srtp_filter_.UnprotectRtp(data, len, &len);
      if (!res) {
        int seq_num = -1;
        uint32 ssrc = 0;
        GetRtpSeqNum(data, len, &seq_num);
        GetRtpSsrc(data, len, &ssrc);
        LOG(LS_ERROR) << "Failed to unprotect " << content_name_
                      << " RTP packet: size=" << len
                      << ", seqnum=" << seq_num << ", SSRC=" << ssrc;
        return;
      }
    } else {
      res = srtp_filter_.UnprotectRtcp(data, len, &len);
      if (!res) {
        int type = -1;
        GetRtcpType(data, len, &type);
        LOG(LS_ERROR) << "Failed to unprotect " << content_name_
                      << " RTCP packet: size=" << len << ", type=" << type;
        return;
      }
    }

    packet->SetLength(len);
  }

  // Push it down to the media channel.
  if (!rtcp) {
    media_channel_->OnPacketReceived(packet);
  } else {
    media_channel_->OnRtcpReceived(packet);
  }
}

void BaseChannel::OnSessionState(BaseSession* session,
                                 BaseSession::State state) {
  const MediaContentDescription* content = NULL;
  switch (state) {
    case Session::STATE_SENTINITIATE:
      content = GetFirstContent(session->local_description());
      if (content && !SetLocalContent(content, CA_OFFER)) {
        LOG(LS_ERROR) << "Failure in SetLocalContent with CA_OFFER";
        session->SetError(BaseSession::ERROR_CONTENT);
      }
      break;
    case Session::STATE_SENTACCEPT:
      content = GetFirstContent(session->local_description());
      if (content && !SetLocalContent(content, CA_ANSWER)) {
        LOG(LS_ERROR) << "Failure in SetLocalContent with CA_ANSWER";
        session->SetError(BaseSession::ERROR_CONTENT);
      }
      break;
    case Session::STATE_RECEIVEDINITIATE:
      content = GetFirstContent(session->remote_description());
      if (content && !SetRemoteContent(content, CA_OFFER)) {
        LOG(LS_ERROR) << "Failure in SetRemoteContent with CA_OFFER";
        session->SetError(BaseSession::ERROR_CONTENT);
      }
      break;
    case Session::STATE_RECEIVEDACCEPT:
      content = GetFirstContent(session->remote_description());
      if (content && !SetRemoteContent(content, CA_ANSWER)) {
        LOG(LS_ERROR) << "Failure in SetRemoteContent with CA_ANSWER";
        session->SetError(BaseSession::ERROR_CONTENT);
      }
      break;
    default:
      break;
  }
}

void BaseChannel::OnRemoteDescriptionUpdate(BaseSession* session) {
  const MediaContentDescription* content =
      GetFirstContent(session->remote_description());

  if (content && !SetRemoteContent(content, CA_UPDATE)) {
    LOG(LS_ERROR) << "Failure in SetRemoteContent with CA_UPDATE";
    session->SetError(BaseSession::ERROR_CONTENT);
  }
}

void BaseChannel::EnableMedia_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (enabled_)
    return;

  LOG(LS_INFO) << "Channel enabled";
  enabled_ = true;
  ChangeState();
}

void BaseChannel::DisableMedia_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (!enabled_)
    return;

  LOG(LS_INFO) << "Channel disabled";
  enabled_ = false;
  ChangeState();
}

void BaseChannel::MuteMedia_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (muted_)
    return;

  if (media_channel()->Mute(true)) {
    LOG(LS_INFO) << "Channel muted";
    muted_ = true;
  }
}

void BaseChannel::UnmuteMedia_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (!muted_)
    return;

  if (media_channel()->Mute(false)) {
    LOG(LS_INFO) << "Channel unmuted";
    muted_ = false;
  }
}

void BaseChannel::ChannelWritable_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (writable_)
    return;
  LOG(LS_INFO) << "Channel socket writable ("
               << transport_channel_->name().c_str() << ")"
               << (was_ever_writable_ ? "" : " for the first time");
  was_ever_writable_ = true;
  writable_ = true;
  ChangeState();
}

void BaseChannel::ChannelNotWritable_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (!writable_)
    return;

  LOG(LS_INFO) << "Channel socket not writable ("
               << transport_channel_->name().c_str() << ")";
  writable_ = false;
  ChangeState();
}

// Sets the maximum video bandwidth for automatic bandwidth adjustment.
bool BaseChannel::SetMaxSendBandwidth_w(int max_bandwidth) {
  return media_channel()->SetSendBandwidth(true, max_bandwidth);
}

bool BaseChannel::SetRtcpCName_w(const std::string& cname) {
  return media_channel()->SetRtcpCName(cname);
}

bool BaseChannel::SetSrtp_w(const std::vector<CryptoParams>& cryptos,
                            ContentAction action, ContentSource src) {
  bool ret;
  if (action == CA_OFFER) {
    ret = srtp_filter_.SetOffer(cryptos, src);
  } else if (action == CA_ANSWER) {
    ret = srtp_filter_.SetAnswer(cryptos, src);
  } else {
    // CA_UPDATE, no crypto params.
    ret = true;
  }
  return ret;
}

bool BaseChannel::SetRtcpMux_w(bool enable, ContentAction action,
                               ContentSource src) {
  bool ret;
  if (action == CA_OFFER) {
    ret = rtcp_mux_filter_.SetOffer(enable, src);
  } else if (action == CA_ANSWER) {
    ret = rtcp_mux_filter_.SetAnswer(enable, src);
    if (ret && rtcp_mux_filter_.IsActive()) {
      // We activated RTCP mux, close down the RTCP transport.
      set_rtcp_transport_channel(NULL);
      // If the RTP transport is already writable, then so are we.
      if (transport_channel_->writable()) {
        ChannelWritable_w();
      }
    }
  } else {
    // CA_UPDATE, no RTCP mux info.
    ret = true;
  }
  return ret;
}

// TODO: Check all of the ssrcs in all of the streams in
// the content, and not just the first one.
bool BaseChannel::SetSsrcMux_w(bool enable,
                               const MediaContentDescription* content,
                               ContentAction action,
                               ContentSource src) {
  bool ret = true;
  if (action == CA_OFFER) {
    ret = ssrc_filter_.SetOffer(enable, src);
    if (ret && src == CS_REMOTE) {  // if received offer with ssrc
      ret = ssrc_filter_.AddStream(content->first_ssrc());
    }
  } else if (action == CA_ANSWER) {
    ret = ssrc_filter_.SetAnswer(enable, src);
    if (ret && src == CS_REMOTE && ssrc_filter_.IsActive()) {
      ret = ssrc_filter_.AddStream(content->first_ssrc());
    }
  }
  return ret;
}

void BaseChannel::OnMessage(talk_base::Message *pmsg) {
  switch (pmsg->message_id) {
    case MSG_ENABLE:
      EnableMedia_w();
      break;
    case MSG_DISABLE:
      DisableMedia_w();
      break;

    case MSG_MUTE:
      MuteMedia_w();
      break;
    case MSG_UNMUTE:
      UnmuteMedia_w();
      break;

    case MSG_SETRTCPCNAME: {
      SetRtcpCNameData* data = static_cast<SetRtcpCNameData*>(pmsg->pdata);
      data->result = SetRtcpCName_w(data->cname);
      break;
    }

    case MSG_SETLOCALCONTENT: {
      SetContentData* data = static_cast<SetContentData*>(pmsg->pdata);
      data->result = SetLocalContent_w(data->content, data->action);
      break;
    }
    case MSG_SETREMOTECONTENT: {
      SetContentData* data = static_cast<SetContentData*>(pmsg->pdata);
      data->result = SetRemoteContent_w(data->content, data->action);
      break;
    }

    case MSG_REMOVESTREAM: {
      StreamMessageData* data = static_cast<StreamMessageData*>(pmsg->pdata);
      RemoveStream_w(data->ssrc1);
      break;
    }

    case MSG_SETMAXSENDBANDWIDTH: {
      SetBandwidthData* data = static_cast<SetBandwidthData*>(pmsg->pdata);
      data->result = SetMaxSendBandwidth_w(data->value);
      break;
    }

    case MSG_RTPPACKET:
    case MSG_RTCPPACKET: {
      PacketMessageData* data = static_cast<PacketMessageData*>(pmsg->pdata);
      SendPacket(pmsg->message_id == MSG_RTCPPACKET, &data->packet);
      delete data;  // because it is Posted
      break;
    }
  }
}

void BaseChannel::Send(uint32 id, talk_base::MessageData *pdata) {
  worker_thread_->Send(this, id, pdata);
}

void BaseChannel::Post(uint32 id, talk_base::MessageData *pdata) {
  worker_thread_->Post(this, id, pdata);
}

void BaseChannel::PostDelayed(int cmsDelay, uint32 id,
                              talk_base::MessageData *pdata) {
  worker_thread_->PostDelayed(cmsDelay, this, id, pdata);
}

void BaseChannel::Clear(uint32 id, talk_base::MessageList* removed) {
  worker_thread_->Clear(this, id, removed);
}

void BaseChannel::FlushRtcpMessages() {
  // Flush all remaining RTCP messages. This should only be called in
  // destructor.
  ASSERT(talk_base::Thread::Current() == worker_thread_);
  talk_base::MessageList rtcp_messages;
  Clear(MSG_RTCPPACKET, &rtcp_messages);
  for (talk_base::MessageList::iterator it = rtcp_messages.begin();
       it != rtcp_messages.end(); ++it) {
    Send(MSG_RTCPPACKET, it->pdata);
  }
}

VoiceChannel::VoiceChannel(talk_base::Thread* thread,
                           MediaEngineInterface* media_engine,
                           VoiceMediaChannel* media_channel,
                           BaseSession* session,
                           const std::string& content_name,
                           bool rtcp)
    : BaseChannel(thread, media_engine, media_channel, session, content_name,
                  rtcp),
      received_media_(false),
      mute_on_type_(false),
      mute_on_type_timeout_(kTypingBlackoutPeriod) {
}

VoiceChannel::~VoiceChannel() {
  StopAudioMonitor();
  StopMediaMonitor();
  // this can't be done in the base class, since it calls a virtual
  DisableMedia_w();
}

bool VoiceChannel::Init() {
  TransportChannel* rtcp_channel = rtcp() ?
      session()->CreateChannel(content_name(), "rtcp") : NULL;
  if (!BaseChannel::Init(session()->CreateChannel(content_name(), "rtp"),
                         rtcp_channel)) {
    return false;
  }
  media_channel()->SignalMediaError.connect(
      this, &VoiceChannel::OnVoiceChannelError);
  srtp_filter()->SignalSrtpError.connect(
      this, &VoiceChannel::OnSrtpError);
  return true;
}

bool VoiceChannel::AddStream(uint32 ssrc) {
  StreamMessageData data(ssrc, 0);
  Send(MSG_ADDSTREAM, &data);
  ssrc_filter()->AddStream(ssrc);
  return true;
}

bool VoiceChannel::SetRingbackTone(const void* buf, int len) {
  SetRingbackToneMessageData data(buf, len);
  Send(MSG_SETRINGBACKTONE, &data);
  return data.result;
}

// TODO: Handle early media the right way. We should get an explicit
// ringing message telling us to start playing local ringback, which we cancel
// if any early media actually arrives. For now, we do the opposite, which is
// to wait 1 second for early media, and start playing local ringback if none
// arrives.
void VoiceChannel::SetEarlyMedia(bool enable) {
  if (enable) {
    // Start the early media timeout
    PostDelayed(kEarlyMediaTimeout, MSG_EARLYMEDIATIMEOUT);
  } else {
    // Stop the timeout if currently going.
    Clear(MSG_EARLYMEDIATIMEOUT);
  }
}

bool VoiceChannel::PlayRingbackTone(uint32 ssrc, bool play, bool loop) {
  PlayRingbackToneMessageData data(ssrc, play, loop);
  Send(MSG_PLAYRINGBACKTONE, &data);
  return data.result;
}

bool VoiceChannel::PressDTMF(int digit, bool playout) {
  DtmfMessageData data(digit, playout);
  Send(MSG_PRESSDTMF, &data);
  return data.result;
}

bool VoiceChannel::SetOutputScaling(uint32 ssrc, double left, double right) {
  ScaleVolumeMessageData data(ssrc, left, right);
  Send(MSG_SCALEVOLUME, &data);
  return data.result;
}

void VoiceChannel::StartMediaMonitor(int cms) {
  media_monitor_.reset(new VoiceMediaMonitor(media_channel(), worker_thread(),
      talk_base::Thread::Current()));
  media_monitor_->SignalUpdate.connect(
      this, &VoiceChannel::OnMediaMonitorUpdate);
  media_monitor_->Start(cms);
}

void VoiceChannel::StopMediaMonitor() {
  if (media_monitor_.get()) {
    media_monitor_->Stop();
    media_monitor_->SignalUpdate.disconnect(this);
    media_monitor_.reset();
  }
}

void VoiceChannel::StartAudioMonitor(int cms) {
  audio_monitor_.reset(new AudioMonitor(this, talk_base::Thread::Current()));
  audio_monitor_
    ->SignalUpdate.connect(this, &VoiceChannel::OnAudioMonitorUpdate);
  audio_monitor_->Start(cms);
}

void VoiceChannel::StopAudioMonitor() {
  if (audio_monitor_.get()) {
    audio_monitor_->Stop();
    audio_monitor_.reset();
  }
}

bool VoiceChannel::IsAudioMonitorRunning() const {
  return (audio_monitor_.get() != NULL);
}

int VoiceChannel::GetInputLevel_w() {
  return media_engine()->GetInputLevel();
}

int VoiceChannel::GetOutputLevel_w() {
  return media_channel()->GetOutputLevel();
}

void VoiceChannel::GetActiveStreams_w(AudioInfo::StreamList* actives) {
  media_channel()->GetActiveStreams(actives);
}

void VoiceChannel::OnChannelRead(TransportChannel* channel,
                                 const char* data, size_t len) {
  BaseChannel::OnChannelRead(channel, data, len);

  // Set a flag when we've received an RTP packet. If we're waiting for early
  // media, this will disable the timeout.
  if (!received_media_ && !PacketIsRtcp(channel, data, len)) {
    received_media_ = true;
  }
}

void VoiceChannel::ChangeState() {
  // Render incoming data if we're the active call, and we have the local
  // content. We receive data on the default channel and multiplexed streams.
  bool recv = enabled() && has_local_content();
  if (!media_channel()->SetPlayout(recv)) {
    SendLastMediaError();
  }
    
  // Send outgoing data if we're the active call, we have the remote content,
  // and we have had some form of connectivity.
  bool send = enabled() && has_remote_content() && was_ever_writable();
  SendFlags send_flag = send ? SEND_MICROPHONE : SEND_NOTHING;
  if (!media_channel()->SetSend(send_flag)) {
    LOG(LS_ERROR) << "Failed to SetSend " << send_flag << " on voice channel";
    SendLastMediaError();
  }

  LOG(LS_INFO) << "Changing voice state, recv=" << recv << " send=" << send;
}

const MediaContentDescription* VoiceChannel::GetFirstContent(
    const SessionDescription* sdesc) {
  const ContentInfo* cinfo = GetFirstAudioContent(sdesc);
  if (cinfo == NULL)
    return NULL;

  return static_cast<const MediaContentDescription*>(cinfo->description);
}

bool VoiceChannel::SetLocalContent_w(const MediaContentDescription* content,
                                     ContentAction action) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  LOG(LS_INFO) << "Setting local voice description";

  const AudioContentDescription* audio =
      static_cast<const AudioContentDescription*>(content);
  ASSERT(audio != NULL);

  bool ret;
  if (audio->has_ssrcs()) {
    // TODO: Handle multiple streams and ssrcs here.
    media_channel()->SetSendSsrc(audio->first_ssrc());
    LOG(LS_INFO) << "Set send ssrc for audio: " << audio->first_ssrc();
  }
  // Set local SRTP parameters (what we will encrypt with).
  ret = SetSrtp_w(audio->cryptos(), action, CS_LOCAL);
  // Set local RTCP mux parameters.
  if (ret) {
    ret = SetRtcpMux_w(audio->rtcp_mux(), action, CS_LOCAL);
  }
  // Set SSRC mux filter
  if (ret) {
    ret = SetSsrcMux_w(audio->has_ssrcs(), content, action, CS_LOCAL);
  }
  // Set local audio codecs (what we want to receive).
  if (ret) {
    ret = media_channel()->SetRecvCodecs(audio->codecs());
  }
  // Set local RTP header extensions.
  if (ret && audio->rtp_header_extensions_set()) {
    ret = media_channel()->SetRecvRtpHeaderExtensions(
        audio->rtp_header_extensions());
  }
  // If everything worked, see if we can start receiving.
  if (ret) {
    set_has_local_content(true);
    ChangeState();
  } else {
    LOG(LS_WARNING) << "Failed to set local voice description";
  }
  return ret;
}

bool VoiceChannel::SetRemoteContent_w(const MediaContentDescription* content,
                                      ContentAction action) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  LOG(LS_INFO) << "Setting remote voice description";

  const AudioContentDescription* audio =
      static_cast<const AudioContentDescription*>(content);
  ASSERT(audio != NULL);

  bool ret;
  // Set remote SRTP parameters (what the other side will encrypt with).
  ret = SetSrtp_w(audio->cryptos(), action, CS_REMOTE);
  // Set remote RTCP mux parameters.
  if (ret) {
    ret = SetRtcpMux_w(audio->rtcp_mux(), action, CS_REMOTE);
  }
  // Set SSRC mux filter
  if (ret) {
    ret = SetSsrcMux_w(audio->has_ssrcs(), content, action, CS_REMOTE);
  }

  // Set remote video codecs (what the other side wants to receive).
  if (ret) {
    ret = media_channel()->SetSendCodecs(audio->codecs());
  }
  // Set remote RTP header extensions.
  if (ret && audio->rtp_header_extensions_set()) {
    ret = media_channel()->SetSendRtpHeaderExtensions(
        audio->rtp_header_extensions());
  }

  // Tweak our audio processing settings, if needed.
  int audio_options = 0;
  if (audio->conference_mode()) {
    audio_options |= OPT_CONFERENCE;
  }
  if (audio->agc_minus_10db()) {
    audio_options |= OPT_AGC_MINUS_10DB;
  }
  if (!media_channel()->SetOptions(audio_options)) {
    // Log an error on failure, but don't abort the call.
    LOG(LS_ERROR) << "Failed to set voice channel options";
  }

  // If everything worked, see if we can start sending.
  if (ret) {
    set_has_remote_content(true);
    ChangeState();
  } else {
    LOG(LS_WARNING) << "Failed to set remote voice description";
  }
  return ret;
}

void VoiceChannel::AddStream_w(uint32 ssrc) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  media_channel()->AddStream(ssrc);
}

void VoiceChannel::RemoveStream_w(uint32 ssrc) {
  media_channel()->RemoveStream(ssrc);
}

bool VoiceChannel::SetRingbackTone_w(const void* buf, int len) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  return media_channel()->SetRingbackTone(static_cast<const char*>(buf), len);
}

bool VoiceChannel::PlayRingbackTone_w(uint32 ssrc, bool play, bool loop) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  if (play) {
    LOG(LS_INFO) << "Playing ringback tone, loop=" << loop;
  } else {
    LOG(LS_INFO) << "Stopping ringback tone";
  }
  return media_channel()->PlayRingbackTone(ssrc, play, loop);
}

void VoiceChannel::HandleEarlyMediaTimeout() {
  // This occurs on the main thread, not the worker thread.
  if (!received_media_) {
    LOG(LS_INFO) << "No early media received before timeout";
    SignalEarlyMediaTimeout(this);
  }
}

bool VoiceChannel::PressDTMF_w(int digit, bool playout) {
  if (!enabled() || !writable()) {
    return false;
  }

  return media_channel()->PressDTMF(digit, playout);
}

bool VoiceChannel::SetOutputScaling_w(uint32 ssrc, double left, double right) {
  return media_channel()->SetOutputScaling(ssrc, left, right);
}

void VoiceChannel::OnMessage(talk_base::Message *pmsg) {
  switch (pmsg->message_id) {
    case MSG_ADDSTREAM: {
      StreamMessageData* data = static_cast<StreamMessageData*>(pmsg->pdata);
      AddStream_w(data->ssrc1);
      break;
    }
    case MSG_SETRINGBACKTONE: {
      SetRingbackToneMessageData* data =
          static_cast<SetRingbackToneMessageData*>(pmsg->pdata);
      data->result = SetRingbackTone_w(data->buf, data->len);
      break;
    }
    case MSG_PLAYRINGBACKTONE: {
      PlayRingbackToneMessageData* data =
          static_cast<PlayRingbackToneMessageData*>(pmsg->pdata);
      data->result = PlayRingbackTone_w(data->ssrc, data->play, data->loop);
      break;
    }
    case MSG_EARLYMEDIATIMEOUT:
      HandleEarlyMediaTimeout();
      break;
    case MSG_PRESSDTMF: {
      DtmfMessageData* data = static_cast<DtmfMessageData*>(pmsg->pdata);
      data->result = PressDTMF_w(data->digit, data->playout);
      break;
    }
    case MSG_SCALEVOLUME: {
      ScaleVolumeMessageData* data =
          static_cast<ScaleVolumeMessageData*>(pmsg->pdata);
      data->result = SetOutputScaling_w(data->ssrc, data->left, data->right);
      break;
    }
    case MSG_CHANNEL_ERROR: {
      VoiceChannelErrorMessageData* data =
          static_cast<VoiceChannelErrorMessageData*>(pmsg->pdata);
      SignalMediaError(this, data->ssrc, data->error);
      delete data;
      break;
    }
    default:
      BaseChannel::OnMessage(pmsg);
      break;
  }
}

void VoiceChannel::OnConnectionMonitorUpdate(
    SocketMonitor* monitor, const std::vector<ConnectionInfo>& infos) {
  SignalConnectionMonitor(this, infos);
}

void VoiceChannel::OnMediaMonitorUpdate(
    VoiceMediaChannel* media_channel, const VoiceMediaInfo& info) {
  ASSERT(media_channel == this->media_channel());
  SignalMediaMonitor(this, info);
}

void VoiceChannel::OnAudioMonitorUpdate(AudioMonitor* monitor,
                                        const AudioInfo& info) {
  SignalAudioMonitor(this, info);
}

void VoiceChannel::OnVoiceChannelError(
    uint32 ssrc, VoiceMediaChannel::Error err) {
  if (err == VoiceMediaChannel::ERROR_REC_TYPING_NOISE_DETECTED &&
      mute_on_type_ && !muted()) {
    Mute(true);
    PostDelayed(mute_on_type_timeout_, MSG_UNMUTE, NULL);
  }
  VoiceChannelErrorMessageData* data = new VoiceChannelErrorMessageData(
      ssrc, err);
  signaling_thread()->Post(this, MSG_CHANNEL_ERROR, data);
}

void VoiceChannel::OnSrtpError(uint32 ssrc, SrtpFilter::Mode mode,
                               SrtpFilter::Error error) {
  switch (error) {
    case SrtpFilter::ERROR_FAIL:
      OnVoiceChannelError(ssrc, (mode == SrtpFilter::PROTECT) ?
                          VoiceMediaChannel::ERROR_REC_SRTP_ERROR :
                          VoiceMediaChannel::ERROR_PLAY_SRTP_ERROR);
      break;
    case SrtpFilter::ERROR_AUTH:
      OnVoiceChannelError(ssrc, (mode == SrtpFilter::PROTECT) ?
                          VoiceMediaChannel::ERROR_REC_SRTP_AUTH_FAILED :
                          VoiceMediaChannel::ERROR_PLAY_SRTP_AUTH_FAILED);
      break;
    case SrtpFilter::ERROR_REPLAY:
      // Only receving channel should have this error.
      ASSERT(mode == SrtpFilter::UNPROTECT);
      OnVoiceChannelError(ssrc, VoiceMediaChannel::ERROR_PLAY_SRTP_REPLAY);
      break;
    default:
      break;
  }
}

VideoChannel::VideoChannel(talk_base::Thread* thread,
                           MediaEngineInterface* media_engine,
                           VideoMediaChannel* media_channel,
                           BaseSession* session,
                           const std::string& content_name,
                           bool rtcp,
                           VoiceChannel* voice_channel)
    : BaseChannel(thread, media_engine, media_channel, session, content_name,
                  rtcp),
      voice_channel_(voice_channel), renderer_(NULL) {
}

bool VideoChannel::Init() {
  TransportChannel* rtcp_channel = rtcp() ?
      session()->CreateChannel(content_name(), "video_rtcp") : NULL;
  if (!BaseChannel::Init(
          session()->CreateChannel(content_name(), "video_rtp"),
          rtcp_channel)) {
    return false;
  }
  media_channel()->SignalScreencastWindowEvent.connect(
      this, &VideoChannel::OnScreencastWindowEvent);
  media_channel()->SignalMediaError.connect(
      this, &VideoChannel::OnVideoChannelError);
  srtp_filter()->SignalSrtpError.connect(
      this, &VideoChannel::OnSrtpError);
  return true;
}

void VoiceChannel::SendLastMediaError() {
  uint32 ssrc;
  VoiceMediaChannel::Error error;
  media_channel()->GetLastMediaError(&ssrc, &error);
  SignalMediaError(this, ssrc, error);
}

VideoChannel::~VideoChannel() {
  StopMediaMonitor();
  // this can't be done in the base class, since it calls a virtual
  DisableMedia_w();
}

bool VideoChannel::AddStream(uint32 ssrc, uint32 voice_ssrc) {
  StreamMessageData data(ssrc, voice_ssrc);
  Send(MSG_ADDSTREAM, &data);
  ssrc_filter()->AddStream(ssrc);
  return true;
}

bool VideoChannel::SetRenderer(uint32 ssrc, VideoRenderer* renderer) {
  RenderMessageData data(ssrc, renderer);
  Send(MSG_SETRENDERER, &data);
  return true;
}

bool VideoChannel::AddScreencast(uint32 ssrc, talk_base::WindowId id) {
  ScreencastMessageData data(ssrc, id);
  Send(MSG_ADDSCREENCAST, &data);
  return true;
}

bool VideoChannel::RemoveScreencast(uint32 ssrc) {
  ScreencastMessageData data(ssrc, 0);
  Send(MSG_REMOVESCREENCAST, &data);
  return true;
}

bool VideoChannel::SendIntraFrame() {
  Send(MSG_SENDINTRAFRAME);
  return true;
}

bool VideoChannel::RequestIntraFrame() {
  Send(MSG_REQUESTINTRAFRAME);
  return true;
}

void VideoChannel::EnableCpuAdaptation(bool enable) {
  Send(enable ? MSG_ENABLECPUADAPTATION : MSG_DISABLECPUADAPTATION);
}

void VideoChannel::ChangeState() {
  // Render incoming data if we're the active call, and we have the local
  // content. We receive data on the default channel and multiplexed streams.
  bool recv = enabled() && has_local_content();
  if (!media_channel()->SetRender(recv)) {
    LOG(LS_ERROR) << "Failed to SetRender on video channel";
    // TODO: Report error back to server.
  }

  // Send outgoing data if we're the active call, we have the remote content,
  // and we have had some form of connectivity.
  bool send = enabled() && has_remote_content() && was_ever_writable();
  if (!media_channel()->SetSend(send)) {
    LOG(LS_ERROR) << "Failed to SetSend on video channel";
    // TODO: Report error back to server.
  }

  LOG(LS_INFO) << "Changing video state, recv=" << recv << " send=" << send;
}

void VideoChannel::StartMediaMonitor(int cms) {
  media_monitor_.reset(new VideoMediaMonitor(media_channel(), worker_thread(),
      talk_base::Thread::Current()));
  media_monitor_->SignalUpdate.connect(
      this, &VideoChannel::OnMediaMonitorUpdate);
  media_monitor_->Start(cms);
}

void VideoChannel::StopMediaMonitor() {
  if (media_monitor_.get()) {
    media_monitor_->Stop();
    media_monitor_.reset();
  }
}

const MediaContentDescription* VideoChannel::GetFirstContent(
    const SessionDescription* sdesc) {
  const ContentInfo* cinfo = GetFirstVideoContent(sdesc);
  if (cinfo == NULL)
    return NULL;

  return static_cast<const MediaContentDescription*>(cinfo->description);
}

bool VideoChannel::SetLocalContent_w(const MediaContentDescription* content,
                                     ContentAction action) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  LOG(LS_INFO) << "Setting local video description";

  const VideoContentDescription* video =
      static_cast<const VideoContentDescription*>(content);
  ASSERT(video != NULL);

  bool ret;
  if (video->has_ssrcs()) {
    // TODO: Handle multiple streams and ssrcs here.
    media_channel()->SetSendSsrc(video->first_ssrc());
    LOG(LS_INFO) << "Set send ssrc for video: " << video->first_ssrc();
  }
  // Set local SRTP parameters (what we will encrypt with).
  ret = SetSrtp_w(video->cryptos(), action, CS_LOCAL);
  // Set local RTCP mux parameters.
  if (ret) {
    ret = SetRtcpMux_w(video->rtcp_mux(), action, CS_LOCAL);
  }
  // Set SSRC mux filter
  if (ret) {
    ret = SetSsrcMux_w(video->has_ssrcs(), content, action, CS_LOCAL);
  }

  // Set local video codecs (what we want to receive).
  if (ret) {
    ret = media_channel()->SetRecvCodecs(video->codecs());
  }
  // Set local RTP header extensions.
  if (ret && video->rtp_header_extensions_set()) {
    ret = media_channel()->SetRecvRtpHeaderExtensions(
        video->rtp_header_extensions());
  }
  // If everything worked, see if we can start receiving.
  if (ret) {
    set_has_local_content(true);
    ChangeState();
  } else {
    LOG(LS_WARNING) << "Failed to set local video description";
  }
  return ret;
}

bool VideoChannel::SetRemoteContent_w(const MediaContentDescription* content,
                                      ContentAction action) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  LOG(LS_INFO) << "Setting remote video description";

  const VideoContentDescription* video =
      static_cast<const VideoContentDescription*>(content);
  ASSERT(video != NULL);

  bool ret;
  // Set remote SRTP parameters (what the other side will encrypt with).
  ret = SetSrtp_w(video->cryptos(), action, CS_REMOTE);
  // Set remote RTCP mux parameters.
  if (ret) {
    ret = SetRtcpMux_w(video->rtcp_mux(), action, CS_REMOTE);
  }
  // Set SSRC mux filter
  if (ret) {
    ret = SetSsrcMux_w(video->has_ssrcs(), content, action, CS_REMOTE);
  }
  // Set remote video codecs (what the other side wants to receive).
  if (ret) {
    ret = media_channel()->SetSendCodecs(video->codecs());
  }
  // Set remote RTP header extensions.
  if (ret && video->rtp_header_extensions_set()) {
    ret = media_channel()->SetSendRtpHeaderExtensions(
        video->rtp_header_extensions());
  }
  // Set bandwidth parameters (what the other side wants to get, default=auto)
  if (ret) {
    int bandwidth_bps = video->bandwidth();
    bool auto_bandwidth = (bandwidth_bps == kAutoBandwidth);
    ret = media_channel()->SetSendBandwidth(auto_bandwidth, bandwidth_bps);
  }
  // If everything worked, see if we can start sending.
  if (ret) {
    set_has_remote_content(true);
    ChangeState();
  } else {
    LOG(LS_WARNING) << "Failed to set remote video description";
  }
  return ret;
}

void VideoChannel::AddStream_w(uint32 ssrc, uint32 voice_ssrc) {
  media_channel()->AddStream(ssrc, voice_ssrc);
}

void VideoChannel::RemoveStream_w(uint32 ssrc) {
  media_channel()->RemoveStream(ssrc);
}

void VideoChannel::SetRenderer_w(uint32 ssrc, VideoRenderer* renderer) {
  media_channel()->SetRenderer(ssrc, renderer);
}

void VideoChannel::AddScreencast_w(uint32 ssrc, talk_base::WindowId id) {
  media_channel()->AddScreencast(ssrc, id);
}

void VideoChannel::RemoveScreencast_w(uint32 ssrc) {
  media_channel()->RemoveScreencast(ssrc);
}

void VideoChannel::OnScreencastWindowEvent_s(uint32 ssrc,
                                             talk_base::WindowEvent we) {
  ASSERT(signaling_thread() == talk_base::Thread::Current());
  SignalScreencastWindowEvent(ssrc, we);
}

void VideoChannel::OnMessage(talk_base::Message *pmsg) {
  switch (pmsg->message_id) {
    case MSG_ADDSTREAM: {
      StreamMessageData* data = static_cast<StreamMessageData*>(pmsg->pdata);
      AddStream_w(data->ssrc1, data->ssrc2);
      break;
    }
    case MSG_SETRENDERER: {
      RenderMessageData* data = static_cast<RenderMessageData*>(pmsg->pdata);
      SetRenderer_w(data->ssrc, data->renderer);
      break;
    }
    case MSG_ADDSCREENCAST: {
      ScreencastMessageData* data =
          static_cast<ScreencastMessageData*>(pmsg->pdata);
      AddScreencast_w(data->ssrc, data->window_id);
      break;
    }
    case MSG_REMOVESCREENCAST: {
      ScreencastMessageData* data =
          static_cast<ScreencastMessageData*>(pmsg->pdata);
      RemoveScreencast_w(data->ssrc);
      break;
    }
    case MSG_SCREENCASTWINDOWEVENT: {
      ScreencastEventData* data =
          static_cast<ScreencastEventData*>(pmsg->pdata);
      OnScreencastWindowEvent_s(data->ssrc, data->event);
      delete data;
      break;
    }
    case MSG_SENDINTRAFRAME:
      SendIntraFrame_w();
      break;
    case MSG_REQUESTINTRAFRAME:
      RequestIntraFrame_w();
      break;
    case MSG_ENABLECPUADAPTATION:
      EnableCpuAdaptation_w(true);
      break;
    case MSG_DISABLECPUADAPTATION:
      EnableCpuAdaptation_w(false);
      break;
    case MSG_CHANNEL_ERROR: {
      const VideoChannelErrorMessageData* data =
          static_cast<VideoChannelErrorMessageData*>(pmsg->pdata);
      SignalMediaError(this, data->ssrc, data->error);
      delete data;
      break;
    }
    default:
      BaseChannel::OnMessage(pmsg);
      break;
  }
}

void VideoChannel::OnConnectionMonitorUpdate(
    SocketMonitor *monitor, const std::vector<ConnectionInfo> &infos) {
  SignalConnectionMonitor(this, infos);
}

void VideoChannel::OnMediaMonitorUpdate(
    VideoMediaChannel* media_channel, const VideoMediaInfo &info) {
  ASSERT(media_channel == this->media_channel());
  SignalMediaMonitor(this, info);
}

void VideoChannel::OnScreencastWindowEvent(uint32 ssrc,
                                           talk_base::WindowEvent event) {
  ScreencastEventData* pdata = new ScreencastEventData(ssrc, event);
  signaling_thread()->Post(this, MSG_SCREENCASTWINDOWEVENT, pdata);
}

void VideoChannel::OnVideoChannelError(uint32 ssrc,
                                       VideoMediaChannel::Error error) {
  VideoChannelErrorMessageData* data = new VideoChannelErrorMessageData(
      ssrc, error);
  signaling_thread()->Post(this, MSG_CHANNEL_ERROR, data);
}

void VideoChannel::OnSrtpError(uint32 ssrc, SrtpFilter::Mode mode,
                               SrtpFilter::Error error) {
  switch (error) {
    case SrtpFilter::ERROR_FAIL:
      OnVideoChannelError(ssrc, (mode == SrtpFilter::PROTECT) ?
                          VideoMediaChannel::ERROR_REC_SRTP_ERROR :
                          VideoMediaChannel::ERROR_PLAY_SRTP_ERROR);
      break;
    case SrtpFilter::ERROR_AUTH:
      OnVideoChannelError(ssrc, (mode == SrtpFilter::PROTECT) ?
                          VideoMediaChannel::ERROR_REC_SRTP_AUTH_FAILED :
                          VideoMediaChannel::ERROR_PLAY_SRTP_AUTH_FAILED);
      break;
    case SrtpFilter::ERROR_REPLAY:
      // Only receving channel should have this error.
      ASSERT(mode == SrtpFilter::UNPROTECT);
      // TODO: Turn on the signaling of replay error once we have
      // switched to the new mechanism for doing video retransmissions.
      // OnVideoChannelError(ssrc, VideoMediaChannel::ERROR_PLAY_SRTP_REPLAY);
      break;
    default:
      break;
  }
}

}  // namespace cricket
