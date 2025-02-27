/*
 * libjingle
 * Copyright 2004 Google Inc.
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

#include <cstring>
#include <sstream>
#include <deque>
#include <map>

#include "talk/base/basicpacketsocketfactory.h"
#include "talk/base/common.h"
#include "talk/base/gunit.h"
#include "talk/base/helpers.h"
#include "talk/base/host.h"
#include "talk/base/logging.h"
#include "talk/base/natserver.h"
#include "talk/base/natsocketfactory.h"
#include "talk/base/stringencode.h"
#include "talk/p2p/base/constants.h"
#include "talk/p2p/base/parsing.h"
#include "talk/p2p/base/portallocator.h"
#include "talk/p2p/base/p2ptransport.h"
#include "talk/p2p/base/relayport.h"
#include "talk/p2p/base/relayserver.h"
#include "talk/p2p/base/session.h"
#include "talk/p2p/base/sessionclient.h"
#include "talk/p2p/base/sessionmanager.h"
#include "talk/p2p/base/stunport.h"
#include "talk/p2p/base/stunserver.h"
#include "talk/p2p/base/transportchannel.h"
#include "talk/p2p/base/transportchannelproxy.h"
#include "talk/p2p/base/udpport.h"
#include "talk/xmpp/constants.h"

using cricket::SignalingProtocol;
using cricket::PROTOCOL_HYBRID;
using cricket::PROTOCOL_JINGLE;
using cricket::PROTOCOL_GINGLE;

static const std::string kInitiator = "init@init.com";
static const std::string kResponder = "resp@resp.com";
// Expected from test random number generator.
static const std::string kSessionId = "2154761789";
// TODO: When we need to test more than one transport type,
// allow this to be injected like the content types are.
static const std::string kTransportType = "http://www.google.com/transport/p2p";

// Controls how long we wait for a session to send messages that we
// expect, in milliseconds.  We put it high to avoid flaky tests.
static const int kEventTimeout = 5000;

static const int kNumPorts = 2;
static const int kPort0 = 28653;
static const int kPortStep = 5;

static const std::string kNotifyNick1 = "derekcheng_google.com^59422C27";
static const std::string kNotifyNick2 = "someoneelses_google.com^7abd6a7a20";
static const uint32 kNotifyAudioSsrc1 = 2625839801U;
static const uint32 kNotifyAudioSsrc2 = 2529430427U;
static const uint32 kNotifyVideoSsrc1 = 3;
static const uint32 kNotifyVideoSsrc2 = 2;

static const std::string kViewRequestNick = "param_google.com^16A3CDBE";
static const uint32 kViewRequestSsrc = 4;
static const int kViewRequestWidth = 320;
static const int kViewRequestHeight = 200;
static const int kViewRequestFrameRate = 15;

int GetPort(int port_index) {
  return kPort0 + (port_index * kPortStep);
}

std::string GetPortString(int port_index) {
  return talk_base::ToString(GetPort(port_index));
}

// Only works for port_index < 10, which is fine for our purposes.
std::string GetUsername(int port_index) {
  return "username" + std::string(8, talk_base::ToString(port_index)[0]);
}

// Only works for port_index < 10, which is fine for our purposes.
std::string GetPassword(int port_index) {
  return "password" + std::string(8, talk_base::ToString(port_index)[0]);
}

std::string IqAck(const std::string& id,
                  const std::string& from,
                  const std::string& to) {
  return "<cli:iq"
      " to=\"" + to + "\""
      " id=\"" + id + "\""
      " type=\"result\""
      " from=\"" + from + "\""
      " xmlns:cli=\"jabber:client\""
      "/>";
}

std::string IqSet(const std::string& id,
                  const std::string& from,
                  const std::string& to,
                  const std::string& content) {
  return "<cli:iq"
      " to=\"" + to + "\""
      " type=\"set\""
      " from=\"" + from + "\""
      " id=\"" + id + "\""
      " xmlns:cli=\"jabber:client\""
      ">"
      + content +
      "</cli:iq>";
}

std::string IqError(const std::string& id,
                    const std::string& from,
                    const std::string& to,
                    const std::string& content) {
  return "<cli:error"
      " to=\"" + to + "\""
      " type=\"error\""
      " from=\"" + from + "\""
      " id=\"" + id + "\""
      " xmlns:cli=\"jabber:client\""
      ">"
      + content +
      "</cli:error>";
}

std::string GingleSessionXml(const std::string& type,
                             const std::string& content) {
  return "<session"
      " xmlns=\"http://www.google.com/session\""
      " type=\"" + type + "\""
      " id=\"" + kSessionId + "\""
      " initiator=\"" + kInitiator + "\""
      ">"
      + content +
      "</session>";
}

std::string GingleDescriptionXml(const std::string& content_type) {
  return "<description"
      " xmlns=\"" + content_type + "\""
      "/>";
}

std::string P2pCandidateXml(const std::string& name, int port_index) {
  return "<candidate"
      " name=\"" + name + "\""
      " address=\"127.0.0.1\""
      " port=\"" + GetPortString(port_index) + "\""
      " preference=\"1\""
      " username=\"" + GetUsername(port_index) + "\""
      " protocol=\"udp\""
      " generation=\"0\""
      " password=\"" + GetPassword(port_index) + "\""
      " type=\"local\""
      " network=\"network\""
      "/>";
}

std::string JingleActionXml(const std::string& action,
                            const std::string& content) {
  return "<jingle"
      " xmlns=\"urn:xmpp:jingle:1\""
      " action=\"" + action + "\""
      " sid=\"" + kSessionId + "\""
      ">"
      + content +
      "</jingle>";
}

std::string JingleInitiateActionXml(const std::string& content) {
  return "<jingle"
      " xmlns=\"urn:xmpp:jingle:1\""
      " action=\"session-initiate\""
      " sid=\"" + kSessionId + "\""
      " initiator=\"" + kInitiator + "\""
      ">"
      + content +
      "</jingle>";
}

std::string JingleEmptyContentXml(const std::string& content_name,
                                  const std::string& content_type,
                                  const std::string& transport_type) {
  return "<content"
      " name=\"" + content_name + "\""
      " creator=\"initiator\""
      ">"
      "<description"
      " xmlns=\"" + content_type + "\""
      "/>"
      "<transport"
      " xmlns=\"" + transport_type + "\""
      "/>"
      "</content>";
}

std::string JingleContentXml(const std::string& content_name,
                             const std::string& content_type,
                             const std::string& transport_type,
                             const std::string& transport_main) {
  std::string transport = transport_type.empty() ? "" :
      "<transport"
      " xmlns=\"" + transport_type + "\""
      ">"
      + transport_main +
      "</transport>";

  return"<content"
      " name=\"" + content_name + "\""
      " creator=\"initiator\""
      ">"
      "<description"
      " xmlns=\"" + content_type + "\""
      "/>"
      + transport +
      "</content>";
}

std::string JingleTransportContentXml(const std::string& content_name,
                                      const std::string& transport_type,
                                      const std::string& content) {
  return "<content"
      " name=\"" + content_name + "\""
      " creator=\"initiator\""
      ">"
      "<transport"
      " xmlns=\"" + transport_type + "\""
      ">"
      + content +
      "</transport>"
      "</content>";
}

std::string GingleInitiateXml(const std::string& content_type) {
  return GingleSessionXml(
      "initiate",
      GingleDescriptionXml(content_type));
}

std::string JingleInitiateXml(const std::string& content_name_a,
                              const std::string& content_type_a,
                              const std::string& content_name_b,
                              const std::string& content_type_b) {
  if (content_name_b.empty()) {
    return JingleInitiateActionXml(
        JingleEmptyContentXml(
            content_name_a, content_type_a, kTransportType));
  } else {
    return JingleInitiateActionXml(
        JingleEmptyContentXml(
            content_name_a, content_type_a, kTransportType) +
        JingleEmptyContentXml(
            content_name_b, content_type_b, kTransportType));
  }
}

std::string GingleAcceptXml(const std::string& content_type) {
  return GingleSessionXml(
      "accept",
      GingleDescriptionXml(content_type));
}

std::string JingleAcceptXml(const std::string& content_name_a,
                            const std::string& content_type_a,
                            const std::string& content_name_b,
                            const std::string& content_type_b) {
  if (content_name_b.empty()) {
    return JingleActionXml(
        "session-accept",
        JingleEmptyContentXml(
            content_name_a, content_type_a, kTransportType));
  } else {
    return JingleActionXml(
        "session-accept",
        JingleEmptyContentXml(
            content_name_a, content_type_a, kTransportType) +
        JingleEmptyContentXml(
            content_name_b, content_type_b, kTransportType));
  }
}

std::string Gingle2CandidatesXml(const std::string& channel_name,
                                 int port_index0,
                                 int port_index1) {
  return GingleSessionXml(
      "candidates",
      P2pCandidateXml(channel_name, port_index0) +
      P2pCandidateXml(channel_name, port_index1));
}

std::string Gingle4CandidatesXml(const std::string& channel_name_a,
                                 int port_index0,
                                 int port_index1,
                                 const std::string& channel_name_b,
                                 int port_index2,
                                 int port_index3) {
  return GingleSessionXml(
      "candidates",
      P2pCandidateXml(channel_name_a, port_index0) +
      P2pCandidateXml(channel_name_a, port_index1) +
      P2pCandidateXml(channel_name_b, port_index2) +
      P2pCandidateXml(channel_name_b, port_index3));
}

std::string Jingle2TransportInfoXml(const std::string& content_name,
                                    const std::string& channel_name,
                                    int port_index0,
                                    int port_index1) {
  return JingleActionXml(
      "transport-info",
      JingleTransportContentXml(
          content_name, kTransportType,
          P2pCandidateXml(channel_name, port_index0) +
          P2pCandidateXml(channel_name, port_index1)));
}

std::string Jingle4TransportInfoXml(const std::string& content_name,
                                    const std::string& channel_name_a,
                                    int port_index0,
                                    int port_index1,
                                    const std::string& channel_name_b,
                                    int port_index2,
                                    int port_index3) {
  return JingleActionXml(
      "transport-info",
      JingleTransportContentXml(
          content_name, kTransportType,
          P2pCandidateXml(channel_name_a, port_index0) +
          P2pCandidateXml(channel_name_a, port_index1) +
          P2pCandidateXml(channel_name_b, port_index2) +
          P2pCandidateXml(channel_name_b, port_index3)));
}

std::string JingleDescriptionInfoXml(const std::string& content_name,
                                     const std::string& content_type) {
  return JingleActionXml(
      "description-info",
      JingleContentXml(content_name, content_type, "", ""));
}

std::string GingleRejectXml(const std::string& reason) {
  return GingleSessionXml(
      "reject",
      "<" + reason + "/>");
}

std::string JingleTerminateXml(const std::string& reason) {
    return JingleActionXml(
        "session-terminate",
        "<reason><" + reason + "/></reason>");
}

std::string GingleTerminateXml(const std::string& reason) {
  return GingleSessionXml(
      "terminate",
      "<" + reason + "/>");
}

std::string GingleRedirectXml(const std::string& intitiate,
                              const std::string& target) {
  return intitiate +
    "<error code=\"302\" type=\"modify\">"
    "<redirect xmlns=\"http://www.google.com/session\">"
    "xmpp:" + target +
    "</redirect>"
    "</error>";
}

std::string JingleRedirectXml(const std::string& intitiate,
                              const std::string& target) {
  return intitiate +
    "<error code=\"302\" type=\"modify\">"
    "<redirect xmlns=\"urn:ietf:params:xml:ns:xmpp-stanzas\">"
    "xmpp:" + target +
    "</redirect>"
    "</error>";
}

std::string InitiateXml(SignalingProtocol protocol,
                        const std::string& gingle_content_type,
                        const std::string& content_name_a,
                        const std::string& content_type_a,
                        const std::string& content_name_b,
                        const std::string& content_type_b) {
  switch (protocol) {
    case PROTOCOL_JINGLE:
      return JingleInitiateXml(content_name_a, content_type_a,
                               content_name_b, content_type_b);
    case PROTOCOL_GINGLE:
      return GingleInitiateXml(gingle_content_type);
    case PROTOCOL_HYBRID:
      return JingleInitiateXml(content_name_a, content_type_a,
                               content_name_b, content_type_b) +
          GingleInitiateXml(gingle_content_type);
  }
  return "";
}

std::string InitiateXml(SignalingProtocol protocol,
                        const std::string& content_name,
                        const std::string& content_type) {
  return InitiateXml(protocol,
                     content_type,
                     content_name, content_type,
                     "", "");
}

std::string AcceptXml(SignalingProtocol protocol,
                      const std::string& gingle_content_type,
                      const std::string& content_name_a,
                      const std::string& content_type_a,
                      const std::string& content_name_b,
                      const std::string& content_type_b) {
  switch (protocol) {
    case PROTOCOL_JINGLE:
      return JingleAcceptXml(content_name_a, content_type_a,
                             content_name_b, content_type_b);
    case PROTOCOL_GINGLE:
      return GingleAcceptXml(gingle_content_type);
    case PROTOCOL_HYBRID:
      return
          JingleAcceptXml(content_name_a, content_type_a,
                          content_name_b, content_type_b) +
          GingleAcceptXml(gingle_content_type);
  }
  return "";
}


std::string AcceptXml(SignalingProtocol protocol,
                      const std::string& content_name,
                      const std::string& content_type) {
  return AcceptXml(protocol,
                   content_type,
                   content_name, content_type,
                   "", "");
}

std::string TransportInfo2Xml(SignalingProtocol protocol,
                              const std::string& content_name,
                              const std::string& channel_name,
                              int port_index0,
                              int port_index1) {
  switch (protocol) {
    case PROTOCOL_JINGLE:
      return Jingle2TransportInfoXml(
          content_name,
          channel_name, port_index0, port_index1);
    case PROTOCOL_GINGLE:
      return Gingle2CandidatesXml(
          channel_name, port_index0, port_index1);
    case PROTOCOL_HYBRID:
      return
          Jingle2TransportInfoXml(
              content_name,
              channel_name, port_index0, port_index1) +
          Gingle2CandidatesXml(
              channel_name, port_index0, port_index1);
  }
  return "";
}

std::string TransportInfo4Xml(SignalingProtocol protocol,
                              const std::string& content_name,
                              const std::string& channel_name_a,
                              int port_index0,
                              int port_index1,
                              const std::string& channel_name_b,
                              int port_index2,
                              int port_index3) {
  switch (protocol) {
    case PROTOCOL_JINGLE:
      return Jingle4TransportInfoXml(
          content_name,
          channel_name_a, port_index0, port_index1,
          channel_name_b, port_index2, port_index3);
    case PROTOCOL_GINGLE:
      return Gingle4CandidatesXml(
          channel_name_a, port_index0, port_index1,
          channel_name_b, port_index2, port_index3);
    case PROTOCOL_HYBRID:
      return
          Jingle4TransportInfoXml(
              content_name,
              channel_name_a, port_index0, port_index1,
              channel_name_b, port_index2, port_index3) +
          Gingle4CandidatesXml(
              channel_name_a, port_index0, port_index1,
              channel_name_b, port_index2, port_index3);
  }
  return "";
}

std::string RejectXml(SignalingProtocol protocol,
                      const std::string& reason) {
  switch (protocol) {
    case PROTOCOL_JINGLE:
      return JingleTerminateXml(reason);
    case PROTOCOL_GINGLE:
      return GingleRejectXml(reason);
    case PROTOCOL_HYBRID:
      return JingleTerminateXml(reason) +
          GingleRejectXml(reason);
  }
  return "";
}

std::string TerminateXml(SignalingProtocol protocol,
                         const std::string& reason) {
  switch (protocol) {
    case PROTOCOL_JINGLE:
      return JingleTerminateXml(reason);
    case PROTOCOL_GINGLE:
      return GingleTerminateXml(reason);
    case PROTOCOL_HYBRID:
      return JingleTerminateXml(reason) +
          GingleTerminateXml(reason);
  }
  return "";
}

std::string RedirectXml(SignalingProtocol protocol,
                        const std::string& initiate,
                        const std::string& target) {
  switch (protocol) {
    case PROTOCOL_JINGLE:
      return JingleRedirectXml(initiate, target);
    case PROTOCOL_GINGLE:
      return GingleRedirectXml(initiate, target);
  }
  return "";
}

// TODO: Break out and join with fakeportallocator.h
class TestPortAllocatorSession : public cricket::PortAllocatorSession {
 public:
  TestPortAllocatorSession(const std::string& name,
                           const int port_offset)
      : PortAllocatorSession(0),
        name_(name),
        port_offset_(port_offset),
        ports_(kNumPorts),
        address_("127.0.0.1", 0),
        network_("network", "unittest", address_.ipaddr()),
        socket_factory_(talk_base::Thread::Current()),
        running_(false),
        port_(28653) {
  }

  ~TestPortAllocatorSession() {
    for (size_t i = 0; i < ports_.size(); i++)
      delete ports_[i];
  }

  virtual void GetInitialPorts() {
    for (int i = 0; i < kNumPorts; i++) {
      int index = port_offset_ + i;
      ports_[i] = cricket::UDPPort::Create(
          talk_base::Thread::Current(), &socket_factory_,
          &network_, address_.ipaddr(), GetPort(index), GetPort(index));
      ports_[i]->set_username_fragment(GetUsername(index));
      ports_[i]->set_password(GetPassword(index));
      AddPort(ports_[i]);
    }
  }

  virtual void StartGetAllPorts() { running_ = true; }
  virtual void StopGetAllPorts() { running_ = false; }
  virtual bool IsGettingAllPorts() { return running_; }

  void AddPort(cricket::Port* port) {
    port->set_name(name_);
    port->set_preference(1.0);
    port->set_generation(0);
    port->SignalDestroyed.connect(
        this, &TestPortAllocatorSession::OnPortDestroyed);
    port->SignalAddressReady.connect(
        this, &TestPortAllocatorSession::OnAddressReady);
    port->PrepareAddress();
    SignalPortReady(this, port);
  }

  void OnPortDestroyed(cricket::Port* port) {
    for (size_t i = 0; i < ports_.size(); i++) {
      if (ports_[i] == port)
        ports_[i] = NULL;
    }
  }

  void OnAddressReady(cricket::Port* port) {
    SignalCandidatesReady(this, port->candidates());
  }

 private:
  std::string name_;
  int port_offset_;
  std::vector<cricket::Port*> ports_;
  talk_base::SocketAddress address_;
  talk_base::Network network_;
  talk_base::BasicPacketSocketFactory socket_factory_;
  bool running_;
  int port_;
};

class TestPortAllocator : public cricket::PortAllocator {
 public:
  TestPortAllocator() : port_offset_(0) {}

  virtual cricket::PortAllocatorSession*
  CreateSession(const std::string &name,
                const std::string &content_type) {
    port_offset_ += 2;
    return new TestPortAllocatorSession(name, port_offset_ - 2);
  }

  int port_offset_;
};

class TestContentDescription : public cricket::ContentDescription {
 public:
  explicit TestContentDescription(const std::string& gingle_content_type,
                                  const std::string& content_type)
      : gingle_content_type(gingle_content_type),
        content_type(content_type) {
  }

  std::string gingle_content_type;
  std::string content_type;
};

cricket::SessionDescription* NewTestSessionDescription(
    const std::string gingle_content_type,
    const std::string& content_name_a, const std::string& content_type_a,
    const std::string& content_name_b, const std::string& content_type_b) {

  cricket::SessionDescription* offer = new cricket::SessionDescription();
  offer->AddContent(content_name_a, content_type_a,
                    new TestContentDescription(gingle_content_type,
                                               content_type_a));
  if (content_name_a != content_name_b) {
    offer->AddContent(content_name_b, content_type_b,
                      new TestContentDescription(gingle_content_type,
                                                 content_type_b));
  }
  return offer;
}

cricket::SessionDescription* NewTestSessionDescription(
    const std::string& content_name, const std::string& content_type) {

  cricket::SessionDescription* offer = new cricket::SessionDescription();
  offer->AddContent(content_name, content_type,
                    new TestContentDescription(content_type,
                                               content_type));
  return offer;
}

struct TestSessionClient: public cricket::SessionClient,
                          public sigslot::has_slots<> {
 public:
  TestSessionClient() {
  }

  ~TestSessionClient() {
  }

  virtual bool ParseContent(SignalingProtocol protocol,
                            const buzz::XmlElement* elem,
                            const cricket::ContentDescription** content,
                            cricket::ParseError* error) {
    std::string content_type;
    std::string gingle_content_type;
    if (protocol == PROTOCOL_GINGLE) {
      gingle_content_type = elem->Name().Namespace();
    } else {
      content_type = elem->Name().Namespace();
    }

    *content = new TestContentDescription(gingle_content_type, content_type);
    return true;
  }

  virtual bool WriteContent(SignalingProtocol protocol,
                            const cricket::ContentDescription* untyped_content,
                            buzz::XmlElement** elem,
                            cricket::WriteError* error) {
    const TestContentDescription* content =
        static_cast<const TestContentDescription*>(untyped_content);
    std::string content_type = (protocol == PROTOCOL_GINGLE ?
                                content->gingle_content_type :
                                content->content_type);
     *elem = new buzz::XmlElement(
        buzz::QName(content_type, "description"), true);
    return true;
  }

  void OnSessionCreate(cricket::Session* session, bool initiate) {
  }

  void OnSessionDestroy(cricket::Session* session) {
  }
};

struct ChannelHandler : sigslot::has_slots<> {
  explicit ChannelHandler(cricket::TransportChannel* p)
    : channel(p), last_readable(false), last_writable(false), data_count(0),
      last_size(0) {
    p->SignalReadableState.connect(this, &ChannelHandler::OnReadableState);
    p->SignalWritableState.connect(this, &ChannelHandler::OnWritableState);
    p->SignalReadPacket.connect(this, &ChannelHandler::OnReadPacket);
  }

  bool writable() const {
    return last_writable && channel->writable();
  }

  bool readable() const {
    return last_readable && channel->readable();
  }

  void OnReadableState(cricket::TransportChannel* p) {
    EXPECT_EQ(channel, p);
    last_readable = channel->readable();
  }

  void OnWritableState(cricket::TransportChannel* p) {
    EXPECT_EQ(channel, p);
    last_writable = channel->writable();
  }

  void OnReadPacket(cricket::TransportChannel* p, const char* buf,
                    size_t size) {
    EXPECT_EQ(channel, p);
    EXPECT_LE(size, sizeof(last_data));
    data_count += 1;
    last_size = size;
    std::memcpy(last_data, buf, size);
  }

  void Send(const char* data, size_t size) {
    int result = channel->SendPacket(data, size);
    EXPECT_EQ(static_cast<int>(size), result);
  }

  cricket::TransportChannel* channel;
  bool last_readable, last_writable;
  int data_count;
  char last_data[4096];
  size_t last_size;
};

void PrintStanza(const std::string& message,
                 const buzz::XmlElement* stanza) {
  printf("%s: %s\n", message.c_str(), stanza->Str().c_str());
}

class TestClient : public sigslot::has_slots<> {
 public:
  TestClient(cricket::PortAllocator* port_allocator,
             int* next_message_id,
             const std::string& local_name,
             SignalingProtocol start_protocol,
             const std::string& content_type,
             const std::string& content_name_a,
             const std::string& channel_name_a,
             const std::string& content_name_b,
             const std::string& channel_name_b) {
    Construct(port_allocator, next_message_id, local_name, start_protocol,
              content_type, content_name_a, channel_name_a, std::string(""),
              content_name_b, channel_name_b, std::string(""));
  }

  TestClient(cricket::PortAllocator* port_allocator,
             int* next_message_id,
             const std::string& local_name,
             SignalingProtocol start_protocol,
             const std::string& content_type,
             const std::string& content_name_a,
             const std::string& channel_name_a,
             const std::string& channel_name_aa,
             const std::string& content_name_b,
             const std::string& channel_name_b,
             const std::string& channel_name_bb) {
    Construct(port_allocator, next_message_id, local_name, start_protocol,
              content_type, content_name_a, channel_name_a, channel_name_aa,
              content_name_b, channel_name_b, channel_name_bb);
  }

  ~TestClient() {
    if (session) {
      session_manager->DestroySession(session);
      EXPECT_EQ(1U, session_destroyed_count);
    }
    delete session_manager;
    delete client;
  }

  void Construct(cricket::PortAllocator* pa,
                 int* message_id,
                 const std::string& lname,
                 SignalingProtocol protocol,
                 const std::string& cont_type,
                 const std::string& cont_name_a,
                 const std::string& chan_name_a,
                 const std::string& chan_name_aa,
                 const std::string& cont_name_b,
                 const std::string& chan_name_b,
                 const std::string& chan_name_bb) {
    port_allocator_ = pa;
    next_message_id = message_id;
    local_name = lname;
    start_protocol = protocol;
    content_type = cont_type;
    content_name_a = cont_name_a;
    channel_name_a = chan_name_a;
    channel_name_aa = chan_name_aa;
    content_name_b = cont_name_b;
    channel_name_b = chan_name_b;
    channel_name_bb = chan_name_bb;
    session_created_count = 0;
    session_destroyed_count = 0;
    session_remote_description_update_count = 0;
    last_expected_sent_stanza = NULL;
    session = NULL;
    last_session_state = cricket::BaseSession::STATE_INIT;
    chan_a = NULL;
    chan_b = NULL;
    blow_up_on_error = true;
    error_count = 0;
    if (!chan_name_aa.empty() || !chan_name_bb.empty()) {
      chan_aa = NULL;
      chan_bb = NULL;
    }

    session_manager = new cricket::SessionManager(port_allocator_);
    session_manager->SignalSessionCreate.connect(
        this, &TestClient::OnSessionCreate);
    session_manager->SignalSessionDestroy.connect(
        this, &TestClient::OnSessionDestroy);
    session_manager->SignalOutgoingMessage.connect(
        this, &TestClient::OnOutgoingMessage);

    client = new TestSessionClient();
    session_manager->AddClient(content_type, client);
    EXPECT_EQ(client, session_manager->GetClient(content_type));
  }

  uint32 sent_stanza_count() const {
    return sent_stanzas.size();
  }

  const buzz::XmlElement* stanza() const {
    return last_expected_sent_stanza;
  }

  cricket::BaseSession::State session_state() const {
    EXPECT_EQ(last_session_state, session->state());
    return session->state();
  }

  void SetSessionState(cricket::BaseSession::State state) {
    session->SetState(state);
    EXPECT_EQ_WAIT(last_session_state, session->state(), kEventTimeout);
  }

  void CreateSession() {
    session_manager->CreateSession(local_name, content_type);
  }

  void DeliverStanza(const buzz::XmlElement* stanza) {
    session_manager->OnIncomingMessage(stanza);
  }

  void DeliverStanza(const std::string& str) {
    buzz::XmlElement* stanza = buzz::XmlElement::ForStr(str);
    session_manager->OnIncomingMessage(stanza);
    delete stanza;
  }

  void DeliverAckToLastStanza() {
    const buzz::XmlElement* orig_stanza = stanza();
    const buzz::XmlElement* response_stanza =
        buzz::XmlElement::ForStr(IqAck(orig_stanza->Attr(buzz::QN_IQ), "", ""));
    session_manager->OnIncomingResponse(orig_stanza, response_stanza);
    delete response_stanza;
  }

  void ExpectSentStanza(const std::string& expected) {
    EXPECT_TRUE(!sent_stanzas.empty()) <<
        "Found no stanza when expected " << expected;

    last_expected_sent_stanza = sent_stanzas.front();
    sent_stanzas.pop_front();

    std::string actual = last_expected_sent_stanza->Str();
    EXPECT_EQ(expected, actual);
  }

  void SkipUnsentStanza() {
    GetNextOutgoingMessageID();
  }

  bool HasTransport(const std::string& content_name) const {
    ASSERT(session != NULL);
    const cricket::Transport* transport = session->GetTransport(content_name);
    return transport != NULL && (kTransportType == transport->type());
  }

  bool HasChannel(const std::string& content_name,
                  const std::string& channel_name) const {
    ASSERT(session != NULL);
    const cricket::TransportChannel* channel =
        session->GetChannel(content_name, channel_name);
    return channel != NULL && (channel_name == channel->name());
  }

  cricket::TransportChannel* GetChannel(const std::string& content_name,
                                        const std::string& channel_name) const {
    ASSERT(session != NULL);
    return session->GetChannel(content_name, channel_name);
  }

  void OnSessionCreate(cricket::Session* created_session, bool initiate) {
    session_created_count += 1;

    session = created_session;
    session->set_current_protocol(start_protocol);
    session->set_allow_local_ips(true);
    session->SignalState.connect(this, &TestClient::OnSessionState);
    session->SignalError.connect(this, &TestClient::OnSessionError);
    session->SignalRemoteDescriptionUpdate.connect(
        this, &TestClient::OnSessionRemoteDescriptionUpdate);

    CreateChannels();
  }

  void OnSessionDestroy(cricket::Session *session) {
    session_destroyed_count += 1;
  }

  void OnSessionState(cricket::BaseSession* session,
                      cricket::BaseSession::State state) {
    // EXPECT_EQ does not allow use of this, hence the tmp variable.
    cricket::BaseSession* tmp = this->session;
    EXPECT_EQ(tmp, session);
    last_session_state = state;
  }

  void OnSessionError(cricket::BaseSession* session,
                      cricket::BaseSession::Error error) {
    // EXPECT_EQ does not allow use of this, hence the tmp variable.
    cricket::BaseSession* tmp = this->session;
    EXPECT_EQ(tmp, session);
    if (blow_up_on_error) {
      EXPECT_TRUE(false);
    } else {
      error_count++;
    }
  }

  void OnSessionRemoteDescriptionUpdate(cricket::BaseSession* session) {
    session_remote_description_update_count++;
  }

  void PrepareCandidates() {
    session_manager->OnSignalingReady();
  }

  void OnOutgoingMessage(cricket::SessionManager* manager,
                         const buzz::XmlElement* stanza) {
    buzz::XmlElement* elem = new buzz::XmlElement(*stanza);
    EXPECT_TRUE(elem->Name() == buzz::QN_IQ);
    EXPECT_TRUE(elem->HasAttr(buzz::QN_TO));
    EXPECT_FALSE(elem->HasAttr(buzz::QN_FROM));
    EXPECT_TRUE(elem->HasAttr(buzz::QN_TYPE));
    EXPECT_TRUE((elem->Attr(buzz::QN_TYPE) == "set") ||
                (elem->Attr(buzz::QN_TYPE) == "result") ||
                (elem->Attr(buzz::QN_TYPE) == "error"));

    elem->SetAttr(buzz::QN_FROM, local_name);
    if (elem->Attr(buzz::QN_TYPE) == "set") {
      EXPECT_FALSE(elem->HasAttr(buzz::QN_ID));
      elem->SetAttr(buzz::QN_ID, GetNextOutgoingMessageID());
    }

    // Uncommenting this is useful for debugging.
    // PrintStanza("OutgoingMessage", elem);
    sent_stanzas.push_back(elem);
  }

  std::string GetNextOutgoingMessageID() {
    int message_id = (*next_message_id)++;
    std::ostringstream ost;
    ost << message_id;
    return ost.str();
  }

  void CreateChannels() {
    ASSERT(session != NULL);
    chan_a = new ChannelHandler(
        session->CreateChannel(content_name_a, channel_name_a));
    chan_b = new ChannelHandler(
        session->CreateChannel(content_name_b, channel_name_b));
    if (!channel_name_aa.empty() && !channel_name_bb.empty()) {
      chan_aa = new ChannelHandler(
          session->CreateChannel(content_name_a, channel_name_aa));
      chan_bb = new ChannelHandler(
          session->CreateChannel(content_name_b, channel_name_bb));
    }
  }

  int* next_message_id;
  std::string local_name;
  SignalingProtocol start_protocol;
  std::string content_type;
  std::string content_name_a;
  std::string channel_name_a;
  std::string channel_name_aa;
  std::string content_name_b;
  std::string channel_name_b;
  std::string channel_name_bb;

  uint32 session_created_count;
  uint32 session_destroyed_count;
  uint32 session_remote_description_update_count;
  std::deque<buzz::XmlElement*> sent_stanzas;
  buzz::XmlElement* last_expected_sent_stanza;

  cricket::SessionManager* session_manager;
  TestSessionClient* client;
  cricket::PortAllocator* port_allocator_;
  cricket::Session* session;
  cricket::BaseSession::State last_session_state;
  ChannelHandler* chan_a;
  ChannelHandler* chan_aa;
  ChannelHandler* chan_b;
  ChannelHandler* chan_bb;
  bool blow_up_on_error;
  int error_count;
};

class SessionTest : public testing::Test {
 protected:
  virtual void SetUp() {
    // Seed needed for each test to satisfy expectations.
    talk_base::SetRandomTestMode(true);
  }

  virtual void TearDown() {
    talk_base::SetRandomTestMode(false);
  }

  // Tests sending data between two clients, over two channels.
  void TestSendRecv(ChannelHandler* chan1a,
                    ChannelHandler* chan1b,
                    ChannelHandler* chan2a,
                    ChannelHandler* chan2b) {
    const char* dat1a = "spamspamspamspamspamspamspambakedbeansspam";
    const char* dat2a = "mapssnaebdekabmapsmapsmapsmapsmapsmapsmaps";
    const char* dat1b = "Lobster Thermidor a Crevette with a mornay sauce...";
    const char* dat2b = "...ecuas yanrom a htiw etteverC a rodimrehT retsboL";

    for (int i = 0; i < 20; i++) {
      chan1a->Send(dat1a, strlen(dat1a));
      chan1b->Send(dat1b, strlen(dat1b));
      chan2a->Send(dat2a, strlen(dat2a));
      chan2b->Send(dat2b, strlen(dat2b));

      EXPECT_EQ_WAIT(i + 1, chan1a->data_count, kEventTimeout);
      EXPECT_EQ_WAIT(i + 1, chan1b->data_count, kEventTimeout);
      EXPECT_EQ_WAIT(i + 1, chan2a->data_count, kEventTimeout);
      EXPECT_EQ_WAIT(i + 1, chan2b->data_count, kEventTimeout);

      EXPECT_EQ(strlen(dat2a), chan1a->last_size);
      EXPECT_EQ(strlen(dat2b), chan1b->last_size);
      EXPECT_EQ(strlen(dat1a), chan2a->last_size);
      EXPECT_EQ(strlen(dat1b), chan2b->last_size);

      EXPECT_EQ(0, std::memcmp(chan1a->last_data, dat2a,
                               strlen(dat2a)));
      EXPECT_EQ(0, std::memcmp(chan1b->last_data, dat2b,
                               strlen(dat2b)));
      EXPECT_EQ(0, std::memcmp(chan2a->last_data, dat1a,
                               strlen(dat1a)));
      EXPECT_EQ(0, std::memcmp(chan2b->last_data, dat1b,
                               strlen(dat1b)));
    }
  }

  // Test an initiate from one client to another, each with
  // independent initial protocols.  Checks for the correct initiates,
  // candidates, and accept messages, and tests that working network
  // channels are established.
  void TestSession(SignalingProtocol initiator_protocol,
                   SignalingProtocol responder_protocol,
                   SignalingProtocol resulting_protocol,
                   const std::string& gingle_content_type,
                   const std::string& content_type,
                   const std::string& content_name_a,
                   const std::string& channel_name_a,
                   const std::string& content_name_b,
                   const std::string& channel_name_b,
                   const std::string& initiate_xml,
                   const std::string& transport_info_a_xml,
                   const std::string& transport_info_b_xml,
                   const std::string& transport_info_reply_a_xml,
                   const std::string& transport_info_reply_b_xml,
                   const std::string& accept_xml) {
    talk_base::scoped_ptr<cricket::PortAllocator> allocator(
        new TestPortAllocator());
    int next_message_id = 0;

    talk_base::scoped_ptr<TestClient> initiator(
        new TestClient(allocator.get(), &next_message_id,
                       kInitiator, initiator_protocol,
                       content_type,
                       content_name_a,  channel_name_a,
                       content_name_b,  channel_name_b));
    talk_base::scoped_ptr<TestClient> responder(
        new TestClient(allocator.get(), &next_message_id,
                       kResponder, responder_protocol,
                       content_type,
                       content_name_a,  channel_name_a,
                       content_name_b,  channel_name_b));

    // Create Session and check channels and state.
    initiator->CreateSession();
    EXPECT_EQ(1U, initiator->session_created_count);
    EXPECT_EQ(kSessionId, initiator->session->id());
    EXPECT_EQ(initiator->session->local_name(), kInitiator);
    EXPECT_EQ(cricket::BaseSession::STATE_INIT,
              initiator->session_state());

    EXPECT_TRUE(initiator->HasTransport(content_name_a));
    EXPECT_TRUE(initiator->HasChannel(content_name_a, channel_name_a));
    EXPECT_TRUE(initiator->HasTransport(content_name_b));
    EXPECT_TRUE(initiator->HasChannel(content_name_b, channel_name_b));

    // Initiate and expect initiate message sent.
    cricket::SessionDescription* offer = NewTestSessionDescription(
        gingle_content_type,
        content_name_a, content_type,
        content_name_b, content_type);
    EXPECT_TRUE(initiator->session->Initiate(kResponder, offer));
    EXPECT_EQ(initiator->session->remote_name(), kResponder);
    EXPECT_EQ(initiator->session->local_description(), offer);

    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    EXPECT_EQ(cricket::BaseSession::STATE_SENTINITIATE,
              initiator->session_state());
    initiator->ExpectSentStanza(
        IqSet("0", kInitiator, kResponder, initiate_xml));

    // Deliver the initiate. Expect ack and session created with
    // transports.
    responder->DeliverStanza(initiator->stanza());
    responder->ExpectSentStanza(
        IqAck("0", kResponder, kInitiator));
    EXPECT_EQ(0U, responder->sent_stanza_count());

    EXPECT_EQ(1U, responder->session_created_count);
    EXPECT_EQ(kSessionId, responder->session->id());
    EXPECT_EQ(responder->session->local_name(), kResponder);
    EXPECT_EQ(responder->session->remote_name(), kInitiator);
    EXPECT_EQ(cricket::BaseSession::STATE_RECEIVEDINITIATE,
              responder->session_state());

    EXPECT_TRUE(responder->HasTransport(content_name_a));
    EXPECT_TRUE(responder->HasChannel(content_name_a, channel_name_a));
    EXPECT_TRUE(responder->HasTransport(content_name_b));
    EXPECT_TRUE(responder->HasChannel(content_name_b, channel_name_b));

    // Expect transport-info message from initiator.
    // But don't send candidates until initiate ack is received.
    initiator->PrepareCandidates();
    WAIT(initiator->sent_stanza_count() > 0, 100);
    EXPECT_EQ(0U, initiator->sent_stanza_count());
    initiator->DeliverAckToLastStanza();
    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    initiator->ExpectSentStanza(
        IqSet("1", kInitiator, kResponder, transport_info_a_xml));

    // Deliver transport-info and expect ack.
    responder->DeliverStanza(initiator->stanza());
    responder->ExpectSentStanza(
        IqAck("1", kResponder, kInitiator));

    if (!transport_info_b_xml.empty()) {
      // Expect second transport-info message from initiator.
      EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
      initiator->ExpectSentStanza(
          IqSet("2", kInitiator, kResponder, transport_info_b_xml));
      EXPECT_EQ(0U, initiator->sent_stanza_count());

      // Deliver second transport-info message and expect ack.
      responder->DeliverStanza(initiator->stanza());
      responder->ExpectSentStanza(
          IqAck("2", kResponder, kInitiator));
    } else {
      EXPECT_EQ(0U, initiator->sent_stanza_count());
      EXPECT_EQ(0U, responder->sent_stanza_count());
      initiator->SkipUnsentStanza();
    }

    // Expect reply transport-info message from responder.
    responder->PrepareCandidates();
    EXPECT_TRUE_WAIT(responder->sent_stanza_count() > 0, kEventTimeout);
    responder->ExpectSentStanza(
        IqSet("3", kResponder, kInitiator, transport_info_reply_a_xml));

    // Deliver reply transport-info and expect ack.
    initiator->DeliverStanza(responder->stanza());
    initiator->ExpectSentStanza(
        IqAck("3", kInitiator, kResponder));

    if (!transport_info_reply_b_xml.empty()) {
      // Expect second reply transport-info message from responder.
      EXPECT_TRUE_WAIT(responder->sent_stanza_count() > 0, kEventTimeout);
      responder->ExpectSentStanza(
          IqSet("4", kResponder, kInitiator, transport_info_reply_b_xml));
      EXPECT_EQ(0U, responder->sent_stanza_count());

      // Deliver second reply transport-info message and expect ack.
      initiator->DeliverStanza(responder->stanza());
      initiator->ExpectSentStanza(
          IqAck("4", kInitiator, kResponder));
      EXPECT_EQ(0U, initiator->sent_stanza_count());
    } else {
      EXPECT_EQ(0U, initiator->sent_stanza_count());
      EXPECT_EQ(0U, responder->sent_stanza_count());
      responder->SkipUnsentStanza();
    }

    // The channels should be able to become writable at this point.  This
    // requires pinging, so it may take a little while.
    EXPECT_TRUE_WAIT(initiator->chan_a->writable() &&
                     initiator->chan_a->readable(), kEventTimeout);
    EXPECT_TRUE_WAIT(initiator->chan_b->writable() &&
                     initiator->chan_b->readable(), kEventTimeout);
    EXPECT_TRUE_WAIT(responder->chan_a->writable() &&
                     responder->chan_a->readable(), kEventTimeout);
    EXPECT_TRUE_WAIT(responder->chan_b->writable() &&
                     responder->chan_b->readable(), kEventTimeout);

    // Accept the session and expect accept stanza.
    cricket::SessionDescription* answer = NewTestSessionDescription(
        gingle_content_type,
        content_name_a, content_type,
        content_name_b, content_type);
    EXPECT_TRUE(responder->session->Accept(answer));
    EXPECT_EQ(responder->session->local_description(), answer);

    responder->ExpectSentStanza(
        IqSet("5", kResponder, kInitiator, accept_xml));
    EXPECT_EQ(0U, responder->sent_stanza_count());

    // Deliver the accept message and expect an ack.
    initiator->DeliverStanza(responder->stanza());
    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    initiator->ExpectSentStanza(
        IqAck("5", kInitiator, kResponder));
    EXPECT_EQ(0U, initiator->sent_stanza_count());

    // Both sessions should be in progress and have functioning
    // channels.
    EXPECT_EQ(resulting_protocol, initiator->session->current_protocol());
    EXPECT_EQ(resulting_protocol, responder->session->current_protocol());
    EXPECT_EQ_WAIT(cricket::BaseSession::STATE_INPROGRESS,
                   initiator->session_state(), kEventTimeout);
    EXPECT_EQ_WAIT(cricket::BaseSession::STATE_INPROGRESS,
                   responder->session_state(), kEventTimeout);
    TestSendRecv(initiator->chan_a, initiator->chan_b,
                 responder->chan_a, responder->chan_b);

    if (resulting_protocol == PROTOCOL_JINGLE) {
      // Deliver a description-info message to the initiator and check if the
      // content description changes.
      EXPECT_EQ(0U, initiator->session_remote_description_update_count);

      const cricket::SessionDescription* old_session_desc =
          initiator->session->remote_description();
      const cricket::ContentInfo* old_content_a =
          old_session_desc->GetContentByName(content_name_a);
      const cricket::ContentDescription* old_content_desc_a =
          old_content_a->description;
      const cricket::ContentInfo* old_content_b =
          old_session_desc->GetContentByName(content_name_b);
      const cricket::ContentDescription* old_content_desc_b =
          old_content_b->description;
      EXPECT_TRUE(old_content_desc_a != NULL);
      EXPECT_TRUE(old_content_desc_b != NULL);

      LOG(LS_INFO) << "A " << old_content_a->name;
      LOG(LS_INFO) << "B " << old_content_b->name;

      std::string description_info_xml =
          JingleDescriptionInfoXml(content_name_a, content_type);
      initiator->DeliverStanza(
          IqSet("6", kResponder, kInitiator, description_info_xml));
      responder->SkipUnsentStanza();
      EXPECT_EQ(1U, initiator->session_remote_description_update_count);

      const cricket::SessionDescription* new_session_desc =
          initiator->session->remote_description();
      const cricket::ContentInfo* new_content_a =
          new_session_desc->GetContentByName(content_name_a);
      const cricket::ContentDescription* new_content_desc_a =
          new_content_a->description;
      const cricket::ContentInfo* new_content_b =
          new_session_desc->GetContentByName(content_name_b);
      const cricket::ContentDescription* new_content_desc_b =
          new_content_b->description;
      EXPECT_TRUE(new_content_desc_a != NULL);
      EXPECT_TRUE(new_content_desc_b != NULL);
      EXPECT_NE(old_content_desc_a, new_content_desc_a);

      if (content_name_a != content_name_b) {
        // If content_name_a != content_name_b, then b's content description
        // should not have changed since the description-info message only
        // contained an update for content_name_a.
        EXPECT_EQ(old_content_desc_b, new_content_desc_b);
      }

      EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
      initiator->ExpectSentStanza(
          IqAck("6", kInitiator, kResponder));
      EXPECT_EQ(0U, initiator->sent_stanza_count());
    } else {
      responder->SkipUnsentStanza();
    }

    initiator->session->Terminate();
    initiator->ExpectSentStanza(
        IqSet("7", kInitiator, kResponder,
              TerminateXml(resulting_protocol,
                           cricket::STR_TERMINATE_SUCCESS)));

    responder->DeliverStanza(initiator->stanza());
    responder->ExpectSentStanza(
        IqAck("7", kResponder, kInitiator));
    EXPECT_EQ(cricket::BaseSession::STATE_SENTTERMINATE,
              initiator->session_state());
    EXPECT_EQ(cricket::BaseSession::STATE_RECEIVEDTERMINATE,
              responder->session_state());
  }

  // Test an initiate with other content, called "main".
  void TestOtherContent(SignalingProtocol initiator_protocol,
                        SignalingProtocol responder_protocol,
                        SignalingProtocol resulting_protocol) {
    std::string content_name = "main";
    std::string content_type = "http://oink.splat/session";
    std::string content_name_a = content_name;
    std::string channel_name_a = "rtcp";
    std::string content_name_b = content_name;
    std::string channel_name_b = "rtp";
    std::string initiate_xml = InitiateXml(
        initiator_protocol,
        content_name_a, content_type);
    std::string transport_info_a_xml = TransportInfo4Xml(
        initiator_protocol, content_name,
        channel_name_a, 0, 1,
        channel_name_b, 2, 3);
    std::string transport_info_b_xml = "";
    std::string transport_info_reply_a_xml = TransportInfo4Xml(
        resulting_protocol, content_name,
        channel_name_a, 4, 5,
        channel_name_b, 6, 7);
    std::string transport_info_reply_b_xml = "";
    std::string accept_xml = AcceptXml(
        resulting_protocol,
        content_name_a, content_type);


    TestSession(initiator_protocol, responder_protocol, resulting_protocol,
                content_type,
                content_type,
                content_name_a, channel_name_a,
                content_name_b, channel_name_b,
                initiate_xml,
                transport_info_a_xml, transport_info_b_xml,
                transport_info_reply_a_xml, transport_info_reply_b_xml,
                accept_xml);
  }

  // Test an initiate with audio content.
  void TestAudioContent(SignalingProtocol initiator_protocol,
                        SignalingProtocol responder_protocol,
                        SignalingProtocol resulting_protocol) {
    std::string gingle_content_type = cricket::NS_GINGLE_AUDIO;
    std::string content_name = cricket::CN_AUDIO;
    std::string content_type = cricket::NS_JINGLE_RTP;
    std::string channel_name_a = "rtcp";
    std::string channel_name_b = "rtp";
    std::string initiate_xml = InitiateXml(
        initiator_protocol,
        gingle_content_type,
        content_name, content_type,
        "", "");
    std::string transport_info_a_xml = TransportInfo4Xml(
        initiator_protocol, content_name,
        channel_name_a, 0, 1,
        channel_name_b, 2, 3);
    std::string transport_info_b_xml = "";
    std::string transport_info_reply_a_xml = TransportInfo4Xml(
        resulting_protocol, content_name,
        channel_name_a, 4, 5,
        channel_name_b, 6, 7);
    std::string transport_info_reply_b_xml = "";
    std::string accept_xml = AcceptXml(
        resulting_protocol,
        gingle_content_type,
        content_name, content_type,
        "", "");


    TestSession(initiator_protocol, responder_protocol, resulting_protocol,
                gingle_content_type,
                content_type,
                content_name, channel_name_a,
                content_name, channel_name_b,
                initiate_xml,
                transport_info_a_xml, transport_info_b_xml,
                transport_info_reply_a_xml, transport_info_reply_b_xml,
                accept_xml);
  }

  // Since media content is "split" into two contents (audio and
  // video), we need to treat it special.
  void TestVideoContents(SignalingProtocol initiator_protocol,
                         SignalingProtocol responder_protocol,
                         SignalingProtocol resulting_protocol) {
    std::string content_type = cricket::NS_JINGLE_RTP;
    std::string gingle_content_type = cricket::NS_GINGLE_VIDEO;
    std::string content_name_a = cricket::CN_AUDIO;
    std::string channel_name_a = "rtcp";
    std::string content_name_b = cricket::CN_VIDEO;
    std::string channel_name_b = "video_rtp";

    std::string initiate_xml = InitiateXml(
        initiator_protocol,
        gingle_content_type,
        content_name_a, content_type,
        content_name_b, content_type);
    std::string transport_info_a_xml = TransportInfo2Xml(
        initiator_protocol, content_name_a,
        channel_name_a, 0, 1);
    std::string transport_info_b_xml = TransportInfo2Xml(
        initiator_protocol, content_name_b,
        channel_name_b, 2, 3);
    std::string transport_info_reply_a_xml = TransportInfo2Xml(
        resulting_protocol, content_name_a,
        channel_name_a, 4, 5);
    std::string transport_info_reply_b_xml = TransportInfo2Xml(
        resulting_protocol, content_name_b,
        channel_name_b, 6, 7);
    std::string accept_xml = AcceptXml(
        resulting_protocol,
        gingle_content_type,
        content_name_a, content_type,
        content_name_b, content_type);

    TestSession(initiator_protocol, responder_protocol, resulting_protocol,
                gingle_content_type,
                content_type,
                content_name_a, channel_name_a,
                content_name_b, channel_name_b,
                initiate_xml,
                transport_info_a_xml, transport_info_b_xml,
                transport_info_reply_a_xml, transport_info_reply_b_xml,
                accept_xml);
  }

  void TestBadRedirect(SignalingProtocol protocol) {
    std::string content_name = "main";
    std::string content_type = "http://oink.splat/session";
    std::string channel_name_a = "chana";
    std::string channel_name_b = "chanb";
    std::string initiate_xml = InitiateXml(
        protocol, content_name, content_type);
    std::string transport_info_xml = TransportInfo4Xml(
        protocol, content_name,
        channel_name_a, 0, 1,
        channel_name_b, 2, 3);
    std::string transport_info_reply_xml = TransportInfo4Xml(
        protocol, content_name,
        channel_name_a, 4, 5,
        channel_name_b, 6, 7);
    std::string accept_xml = AcceptXml(
        protocol, content_name, content_type);
    std::string responder_full = kResponder + "/full";

    talk_base::scoped_ptr<cricket::PortAllocator> allocator(
        new TestPortAllocator());
    int next_message_id = 0;

    talk_base::scoped_ptr<TestClient> initiator(
        new TestClient(allocator.get(), &next_message_id,
                       kInitiator, protocol,
                       content_type,
                       content_name, channel_name_a,
                       content_name, channel_name_b));

    talk_base::scoped_ptr<TestClient> responder(
        new TestClient(allocator.get(), &next_message_id,
                       responder_full, protocol,
                       content_type,
                       content_name,  channel_name_a,
                       content_name,  channel_name_b));

    // Create Session and check channels and state.
    initiator->CreateSession();
    EXPECT_EQ(1U, initiator->session_created_count);
    EXPECT_EQ(kSessionId, initiator->session->id());
    EXPECT_EQ(initiator->session->local_name(), kInitiator);
    EXPECT_EQ(cricket::BaseSession::STATE_INIT,
              initiator->session_state());

    EXPECT_TRUE(initiator->HasChannel(content_name, channel_name_a));
    EXPECT_TRUE(initiator->HasChannel(content_name, channel_name_b));

    // Initiate and expect initiate message sent.
    cricket::SessionDescription* offer = NewTestSessionDescription(
        content_name, content_type);
    EXPECT_TRUE(initiator->session->Initiate(kResponder, offer));
    EXPECT_EQ(initiator->session->remote_name(), kResponder);
    EXPECT_EQ(initiator->session->local_description(), offer);

    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    EXPECT_EQ(cricket::BaseSession::STATE_SENTINITIATE,
              initiator->session_state());
    initiator->ExpectSentStanza(
        IqSet("0", kInitiator, kResponder, initiate_xml));

    // Expect transport-info message from initiator.
    initiator->DeliverAckToLastStanza();
    initiator->PrepareCandidates();
    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    initiator->ExpectSentStanza(
        IqSet("1", kInitiator, kResponder, transport_info_xml));

    // Send an unauthorized redirect to the initiator and expect it be ignored.
    initiator->blow_up_on_error = false;
    const buzz::XmlElement* initiate_stanza = initiator->stanza();
    talk_base::scoped_ptr<buzz::XmlElement> redirect_stanza(
        buzz::XmlElement::ForStr(
            IqError("ER", kResponder, kInitiator,
                    RedirectXml(protocol, initiate_xml, "not@allowed.com"))));
    initiator->session_manager->OnFailedSend(
        initiate_stanza, redirect_stanza.get());
    EXPECT_EQ(initiator->session->remote_name(), kResponder);
    initiator->blow_up_on_error = true;
    EXPECT_EQ(initiator->error_count, 1);
  }

  void TestGoodRedirect(SignalingProtocol protocol) {
    std::string content_name = "main";
    std::string content_type = "http://oink.splat/session";
    std::string channel_name_a = "chana";
    std::string channel_name_b = "chanb";
    std::string initiate_xml = InitiateXml(
        protocol, content_name, content_type);
    std::string transport_info_xml = TransportInfo4Xml(
        protocol, content_name,
        channel_name_a, 0, 1,
        channel_name_b, 2, 3);
    std::string transport_info_reply_xml = TransportInfo4Xml(
        protocol, content_name,
        channel_name_a, 4, 5,
        channel_name_b, 6, 7);
    std::string accept_xml = AcceptXml(
        protocol, content_name, content_type);
    std::string responder_full = kResponder + "/full";

    talk_base::scoped_ptr<cricket::PortAllocator> allocator(
        new TestPortAllocator());
    int next_message_id = 0;

    talk_base::scoped_ptr<TestClient> initiator(
        new TestClient(allocator.get(), &next_message_id,
                       kInitiator, protocol,
                       content_type,
                       content_name, channel_name_a,
                       content_name, channel_name_b));

    talk_base::scoped_ptr<TestClient> responder(
        new TestClient(allocator.get(), &next_message_id,
                       responder_full, protocol,
                       content_type,
                       content_name,  channel_name_a,
                       content_name,  channel_name_b));

    // Create Session and check channels and state.
    initiator->CreateSession();
    EXPECT_EQ(1U, initiator->session_created_count);
    EXPECT_EQ(kSessionId, initiator->session->id());
    EXPECT_EQ(initiator->session->local_name(), kInitiator);
    EXPECT_EQ(cricket::BaseSession::STATE_INIT,
              initiator->session_state());

    EXPECT_TRUE(initiator->HasChannel(content_name, channel_name_a));
    EXPECT_TRUE(initiator->HasChannel(content_name, channel_name_b));

    // Initiate and expect initiate message sent.
    cricket::SessionDescription* offer = NewTestSessionDescription(
        content_name, content_type);
    EXPECT_TRUE(initiator->session->Initiate(kResponder, offer));
    EXPECT_EQ(initiator->session->remote_name(), kResponder);
    EXPECT_EQ(initiator->session->local_description(), offer);

    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    EXPECT_EQ(cricket::BaseSession::STATE_SENTINITIATE,
              initiator->session_state());
    initiator->ExpectSentStanza(
        IqSet("0", kInitiator, kResponder, initiate_xml));

    // Expect transport-info message from initiator.
    initiator->DeliverAckToLastStanza();
    initiator->PrepareCandidates();
    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    initiator->ExpectSentStanza(
        IqSet("1", kInitiator, kResponder, transport_info_xml));

    // Send a redirect to the initiator and expect all of the message
    // to be resent.
    const buzz::XmlElement* initiate_stanza = initiator->stanza();
    talk_base::scoped_ptr<buzz::XmlElement> redirect_stanza(
        buzz::XmlElement::ForStr(
            IqError("ER2", kResponder, kInitiator,
                    RedirectXml(protocol, initiate_xml, responder_full))));
    initiator->session_manager->OnFailedSend(
        initiate_stanza, redirect_stanza.get());
    EXPECT_EQ(initiator->session->remote_name(), responder_full);

    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    initiator->ExpectSentStanza(
        IqSet("2", kInitiator, responder_full, initiate_xml));
    initiator->ExpectSentStanza(
        IqSet("3", kInitiator, responder_full, transport_info_xml));

    // Deliver the initiate. Expect ack and session created with
    // transports.
    responder->DeliverStanza(
        IqSet("2", kInitiator, responder_full, initiate_xml));
    responder->ExpectSentStanza(
        IqAck("2", responder_full, kInitiator));
    EXPECT_EQ(0U, responder->sent_stanza_count());

    EXPECT_EQ(1U, responder->session_created_count);
    EXPECT_EQ(kSessionId, responder->session->id());
    EXPECT_EQ(responder->session->local_name(), responder_full);
    EXPECT_EQ(responder->session->remote_name(), kInitiator);
    EXPECT_EQ(cricket::BaseSession::STATE_RECEIVEDINITIATE,
              responder->session_state());

    EXPECT_TRUE(responder->HasChannel(content_name, channel_name_a));
    EXPECT_TRUE(responder->HasChannel(content_name, channel_name_b));

    // Deliver transport-info and expect ack.
    responder->DeliverStanza(
        IqSet("3", kInitiator, responder_full, transport_info_xml));
    responder->ExpectSentStanza(
        IqAck("3", responder_full, kInitiator));

    // Expect reply transport-infos sent to new remote JID
    responder->PrepareCandidates();
    EXPECT_TRUE_WAIT(responder->sent_stanza_count() > 0, kEventTimeout);
    responder->ExpectSentStanza(
        IqSet("4", responder_full, kInitiator, transport_info_reply_xml));

    initiator->DeliverStanza(responder->stanza());
    initiator->ExpectSentStanza(
        IqAck("4", kInitiator, responder_full));

    // The channels should be able to become writable at this point.  This
    // requires pinging, so it may take a little while.
    EXPECT_TRUE_WAIT(initiator->chan_a->writable() &&
                     initiator->chan_a->readable(), kEventTimeout);
    EXPECT_TRUE_WAIT(initiator->chan_b->writable() &&
                     initiator->chan_b->readable(), kEventTimeout);
    EXPECT_TRUE_WAIT(responder->chan_a->writable() &&
                     responder->chan_a->readable(), kEventTimeout);
    EXPECT_TRUE_WAIT(responder->chan_b->writable() &&
                     responder->chan_b->readable(), kEventTimeout);

    // Accept the session and expect accept stanza.
    cricket::SessionDescription* answer = NewTestSessionDescription(
        content_name, content_type);
    EXPECT_TRUE(responder->session->Accept(answer));
    EXPECT_EQ(responder->session->local_description(), answer);

    responder->ExpectSentStanza(
        IqSet("5", responder_full, kInitiator, accept_xml));
    EXPECT_EQ(0U, responder->sent_stanza_count());

    // Deliver the accept message and expect an ack.
    initiator->DeliverStanza(responder->stanza());
    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    initiator->ExpectSentStanza(
        IqAck("5", kInitiator, responder_full));
    EXPECT_EQ(0U, initiator->sent_stanza_count());

    // Both sessions should be in progress and have functioning
    // channels.
    EXPECT_EQ_WAIT(cricket::BaseSession::STATE_INPROGRESS,
                   initiator->session_state(), kEventTimeout);
    EXPECT_EQ_WAIT(cricket::BaseSession::STATE_INPROGRESS,
                   responder->session_state(), kEventTimeout);
    TestSendRecv(initiator->chan_a, initiator->chan_b,
                 responder->chan_a, responder->chan_b);
  }

  void TestCandidatesInInitiateAndAccept(const std::string& test_name) {
    std::string content_name = "main";
    std::string content_type = "http://oink.splat/session";
    std::string channel_name_a = "rtcp";
    std::string channel_name_b = "rtp";
    cricket::SignalingProtocol protocol = PROTOCOL_JINGLE;

    talk_base::scoped_ptr<cricket::PortAllocator> allocator(
        new TestPortAllocator());
    int next_message_id = 0;

    talk_base::scoped_ptr<TestClient> initiator(
        new TestClient(allocator.get(), &next_message_id,
                       kInitiator, protocol,
                       content_type,
                       content_name,  channel_name_a,
                       content_name,  channel_name_b));

    talk_base::scoped_ptr<TestClient> responder(
        new TestClient(allocator.get(), &next_message_id,
                       kResponder, protocol,
                       content_type,
                       content_name,  channel_name_a,
                       content_name,  channel_name_b));

    // Create Session and check channels and state.
    initiator->CreateSession();
    EXPECT_TRUE(initiator->HasTransport(content_name));
    EXPECT_TRUE(initiator->HasChannel(content_name, channel_name_a));
    EXPECT_TRUE(initiator->HasTransport(content_name));
    EXPECT_TRUE(initiator->HasChannel(content_name, channel_name_b));

    // Initiate and expect initiate message sent.
    cricket::SessionDescription* offer = NewTestSessionDescription(
        content_name, content_type);
    EXPECT_TRUE(initiator->session->Initiate(kResponder, offer));

    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    EXPECT_EQ(cricket::BaseSession::STATE_SENTINITIATE,
              initiator->session_state());
    initiator->ExpectSentStanza(
        IqSet("0", kInitiator, kResponder,
              InitiateXml(protocol, content_name, content_type)));

    // Fake the delivery the initiate and candidates together.
    responder->DeliverStanza(
        IqSet("A", kInitiator, kResponder,
            JingleInitiateActionXml(
                JingleContentXml(
                    content_name, content_type, kTransportType,
                    P2pCandidateXml(channel_name_a, 0) +
                    P2pCandidateXml(channel_name_a, 1) +
                    P2pCandidateXml(channel_name_b, 2) +
                    P2pCandidateXml(channel_name_b, 3)))));
    responder->ExpectSentStanza(
        IqAck("A", kResponder, kInitiator));
    EXPECT_EQ(0U, responder->sent_stanza_count());

    EXPECT_EQ(1U, responder->session_created_count);
    EXPECT_EQ(kSessionId, responder->session->id());
    EXPECT_EQ(responder->session->local_name(), kResponder);
    EXPECT_EQ(responder->session->remote_name(), kInitiator);
    EXPECT_EQ(cricket::BaseSession::STATE_RECEIVEDINITIATE,
              responder->session_state());

    EXPECT_TRUE(responder->HasTransport(content_name));
    EXPECT_TRUE(responder->HasChannel(content_name, channel_name_a));
    EXPECT_TRUE(responder->HasTransport(content_name));
    EXPECT_TRUE(responder->HasChannel(content_name, channel_name_b));

    // Expect transport-info message from initiator.
    // But don't send candidates until initiate ack is received.
    initiator->DeliverAckToLastStanza();
    initiator->PrepareCandidates();
    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    initiator->ExpectSentStanza(
        IqSet("1", kInitiator, kResponder,
              TransportInfo4Xml(protocol, content_name,
                                channel_name_a, 0, 1,
                                channel_name_b, 2, 3)));

    responder->PrepareCandidates();
    EXPECT_TRUE_WAIT(responder->sent_stanza_count() > 0, kEventTimeout);
    responder->ExpectSentStanza(
        IqSet("2", kResponder, kInitiator,
              TransportInfo4Xml(protocol, content_name,
                                channel_name_a, 4, 5,
                                channel_name_b, 6, 7)));

    // Accept the session and expect accept stanza.
    cricket::SessionDescription* answer = NewTestSessionDescription(
        content_name, content_type);
    EXPECT_TRUE(responder->session->Accept(answer));

    responder->ExpectSentStanza(
        IqSet("3", kResponder, kInitiator,
              AcceptXml(protocol, content_name, content_type)));
    EXPECT_EQ(0U, responder->sent_stanza_count());

    // Fake the delivery the accept and candidates together.
    initiator->DeliverStanza(
        IqSet("B", kResponder, kInitiator,
            JingleActionXml("session-accept",
                JingleContentXml(
                    content_name, content_type, kTransportType,
                    P2pCandidateXml(channel_name_a, 4) +
                    P2pCandidateXml(channel_name_a, 5) +
                    P2pCandidateXml(channel_name_b, 6) +
                    P2pCandidateXml(channel_name_b, 7)))));
    EXPECT_TRUE_WAIT(initiator->sent_stanza_count() > 0, kEventTimeout);
    initiator->ExpectSentStanza(
        IqAck("B", kInitiator, kResponder));
    EXPECT_EQ(0U, initiator->sent_stanza_count());

    // The channels should be able to become writable at this point.  This
    // requires pinging, so it may take a little while.
    EXPECT_TRUE_WAIT(initiator->chan_a->writable() &&
                     initiator->chan_a->readable(), kEventTimeout);
    EXPECT_TRUE_WAIT(initiator->chan_b->writable() &&
                     initiator->chan_b->readable(), kEventTimeout);
    EXPECT_TRUE_WAIT(responder->chan_a->writable() &&
                     responder->chan_a->readable(), kEventTimeout);
    EXPECT_TRUE_WAIT(responder->chan_b->writable() &&
                     responder->chan_b->readable(), kEventTimeout);


    // Both sessions should be in progress and have functioning
    // channels.
    EXPECT_EQ(protocol, initiator->session->current_protocol());
    EXPECT_EQ(protocol, responder->session->current_protocol());
    EXPECT_EQ_WAIT(cricket::BaseSession::STATE_INPROGRESS,
                   initiator->session_state(), kEventTimeout);
    EXPECT_EQ_WAIT(cricket::BaseSession::STATE_INPROGRESS,
                   responder->session_state(), kEventTimeout);
    TestSendRecv(initiator->chan_a, initiator->chan_b,
                 responder->chan_a, responder->chan_b);
  }

  // Tests that when an initiator terminates right after initiate,
  // everything behaves correctly.
  void TestEarlyTerminationFromInitiator(SignalingProtocol protocol) {
    std::string content_name = "main";
    std::string content_type = "http://oink.splat/session";

    talk_base::scoped_ptr<cricket::PortAllocator> allocator(
        new TestPortAllocator());
    int next_message_id = 0;

    talk_base::scoped_ptr<TestClient> initiator(
        new TestClient(allocator.get(), &next_message_id,
                       kInitiator, protocol,
                       content_type,
                       content_name, "a",
                       content_name, "b"));

    talk_base::scoped_ptr<TestClient> responder(
        new TestClient(allocator.get(), &next_message_id,
                       kResponder, protocol,
                       content_type,
                       content_name,  "a",
                       content_name,  "b"));

    // Send initiate
    initiator->CreateSession();
    EXPECT_TRUE(initiator->session->Initiate(
        kResponder, NewTestSessionDescription(content_name, content_type)));
    initiator->ExpectSentStanza(
        IqSet("0", kInitiator, kResponder,
              InitiateXml(protocol, content_name, content_type)));
    EXPECT_EQ(cricket::BaseSession::STATE_SENTINITIATE,
              initiator->session_state());

    responder->DeliverStanza(initiator->stanza());
    responder->ExpectSentStanza(
        IqAck("0", kResponder, kInitiator));
    EXPECT_EQ(cricket::BaseSession::STATE_RECEIVEDINITIATE,
              responder->session_state());

    initiator->session->TerminateWithReason(cricket::STR_TERMINATE_ERROR);
    initiator->ExpectSentStanza(
        IqSet("1", kInitiator, kResponder,
              TerminateXml(protocol, cricket::STR_TERMINATE_ERROR)));
    EXPECT_EQ(cricket::BaseSession::STATE_SENTTERMINATE,
              initiator->session_state());

    responder->DeliverStanza(initiator->stanza());
    responder->ExpectSentStanza(
        IqAck("1", kResponder, kInitiator));
    EXPECT_EQ(cricket::BaseSession::STATE_RECEIVEDTERMINATE,
              responder->session_state());
  }

  // Tests that when the responder rejects, everything behaves
  // correctly.
  void TestRejection(SignalingProtocol protocol) {
    std::string content_name = "main";
    std::string content_type = "http://oink.splat/session";

    talk_base::scoped_ptr<cricket::PortAllocator> allocator(
        new TestPortAllocator());
    int next_message_id = 0;

    talk_base::scoped_ptr<TestClient> initiator(
        new TestClient(allocator.get(), &next_message_id,
                       kInitiator, protocol,
                       content_type,
                       content_name, "a",
                       content_name, "b"));

    // Send initiate
    initiator->CreateSession();
    EXPECT_TRUE(initiator->session->Initiate(
        kResponder, NewTestSessionDescription(content_name, content_type)));
    initiator->ExpectSentStanza(
        IqSet("0", kInitiator, kResponder,
              InitiateXml(protocol, content_name, content_type)));
    EXPECT_EQ(cricket::BaseSession::STATE_SENTINITIATE,
              initiator->session_state());

    initiator->DeliverStanza(
        IqSet("1", kResponder, kInitiator,
              RejectXml(protocol, cricket::STR_TERMINATE_ERROR)));
    initiator->ExpectSentStanza(
        IqAck("1", kInitiator, kResponder));
    if (protocol == PROTOCOL_JINGLE) {
      EXPECT_EQ(cricket::BaseSession::STATE_RECEIVEDTERMINATE,
                initiator->session_state());
    } else {
      EXPECT_EQ(cricket::BaseSession::STATE_RECEIVEDREJECT,
                initiator->session_state());
    }
  }

  void TestTransportMux() {
    std::string content_type = cricket::NS_JINGLE_RTP;
    std::string gingle_content_type = cricket::NS_GINGLE_VIDEO;
    std::string content_name_a = cricket::CN_AUDIO;
    std::string channel_name_a = "rtp";
    std::string channel_name_aa = "rtcp";
    std::string content_name_b = cricket::CN_VIDEO;
    std::string channel_name_b = "video_rtp";
    std::string channel_name_bb = "video_rtcp";
    cricket::SignalingProtocol protocol = PROTOCOL_JINGLE;

    talk_base::scoped_ptr<cricket::PortAllocator> allocator(
        new TestPortAllocator());
    int next_message_id = 0;

    talk_base::scoped_ptr<TestClient> initiator(
        new TestClient(allocator.get(), &next_message_id,
                       kInitiator, protocol,
                       content_type,
                       content_name_a, channel_name_a, channel_name_aa,
                       content_name_b, channel_name_b, channel_name_bb));

    // First creating the offer and answer session descriptions required for
    // testing.
    cricket::SessionDescription* offer = NewTestSessionDescription(
        gingle_content_type,
        content_name_a, content_type,
        content_name_b, content_type);
    // Add group information to the offer
    cricket::ContentGroup group(cricket::GN_TOGETHER);
    group.AddContentName(content_name_a);
    group.AddContentName(content_name_b);
    EXPECT_TRUE(group.HasContentName(content_name_a));
    EXPECT_TRUE(group.HasContentName(content_name_b));
    offer->AddGroup(group);

    // Creating answer for the offer.
    cricket::SessionDescription* answer = NewTestSessionDescription(
        gingle_content_type,
        content_name_a, content_type,
        content_name_b, content_type);
    // Check if group "TOGETHER" exists in the offer. If it's present then
    // remote supports muxing.
    EXPECT_TRUE(offer->HasGroup(cricket::GN_TOGETHER));
    const cricket::ContentGroup* group_offer =
        offer->GetGroupByName(cricket::GN_TOGETHER);
    // Not creating new copy in answer, for test we can use this for test.
    answer->AddGroup(*group_offer);
    EXPECT_TRUE(answer->HasGroup(cricket::GN_TOGETHER));

    initiator->CreateSession();
    EXPECT_TRUE(initiator->session->Initiate(
        kResponder, offer));

    EXPECT_TRUE(initiator->HasTransport(content_name_a));
    EXPECT_TRUE(initiator->HasChannel(content_name_a, channel_name_a));
    EXPECT_TRUE(initiator->HasChannel(content_name_a, channel_name_aa));
    EXPECT_TRUE(initiator->HasTransport(content_name_b));
    EXPECT_TRUE(initiator->HasChannel(content_name_b, channel_name_b));
    EXPECT_TRUE(initiator->HasChannel(content_name_b, channel_name_bb));
    // This test will not create initiator and responder. Manually change
    // session state and invoke methods.
    initiator->PrepareCandidates();
    EXPECT_EQ(cricket::BaseSession::STATE_SENTINITIATE,
              initiator->session_state());
    // Now apply answer to the session and move session state to
    // STATE_RECEIVEDACCEPT
    initiator->session->set_remote_description(answer);
    initiator->session->SetState(cricket::BaseSession::STATE_RECEIVEDACCEPT);
    cricket::TransportChannel* chan_a =
        initiator->GetChannel(content_name_a, channel_name_a);
    cricket::TransportChannel* chan_b =
            initiator->GetChannel(content_name_b, channel_name_b);
    // Since we know these are TransportChannelProxy, type cast it.
    cricket::TransportChannelProxy* proxy_chan_a =
        static_cast<cricket::TransportChannelProxy*>(chan_a);
    cricket::TransportChannelProxy* proxy_chan_b =
            static_cast<cricket::TransportChannelProxy*>(chan_b);
    EXPECT_EQ(proxy_chan_a->impl(), proxy_chan_b->impl());
    cricket::TransportChannel* chan_aa =
            initiator->GetChannel(content_name_a, channel_name_aa);
        cricket::TransportChannel* chan_bb =
                initiator->GetChannel(content_name_b, channel_name_bb);
    cricket::TransportChannelProxy* proxy_chan_aa =
        static_cast<cricket::TransportChannelProxy*>(chan_aa);
    cricket::TransportChannelProxy* proxy_chan_bb =
            static_cast<cricket::TransportChannelProxy*>(chan_bb);
    EXPECT_EQ(proxy_chan_aa->impl(), proxy_chan_bb->impl());
    // TODO - Add test code to send data after mux is enabled.
  }
};

// For each of these, "X => Y = Z" means "if a client with protocol X
// initiates to a client with protocol Y, they end up speaking protocol Z.

// Gingle => Gingle = Gingle (with other content)
TEST_F(SessionTest, GingleToGingleOtherContent) {
  TestOtherContent(PROTOCOL_GINGLE, PROTOCOL_GINGLE, PROTOCOL_GINGLE);
}

// Gingle => Gingle = Gingle (with audio content)
TEST_F(SessionTest, GingleToGingleAudioContent) {
  TestAudioContent(PROTOCOL_GINGLE, PROTOCOL_GINGLE, PROTOCOL_GINGLE);
}

// Gingle => Gingle = Gingle (with video contents)
TEST_F(SessionTest, GingleToGingleVideoContents) {
  TestVideoContents(PROTOCOL_GINGLE, PROTOCOL_GINGLE, PROTOCOL_GINGLE);
}


// Jingle => Jingle = Jingle (with other content)
TEST_F(SessionTest, JingleToJingleOtherContent) {
  TestOtherContent(PROTOCOL_JINGLE, PROTOCOL_JINGLE, PROTOCOL_JINGLE);
}

// Jingle => Jingle = Jingle (with audio content)
TEST_F(SessionTest, JingleToJingleAudioContent) {
  TestAudioContent(PROTOCOL_JINGLE, PROTOCOL_JINGLE, PROTOCOL_JINGLE);
}

// Jingle => Jingle = Jingle (with video contents)
TEST_F(SessionTest, JingleToJingleVideoContents) {
  TestVideoContents(PROTOCOL_JINGLE, PROTOCOL_JINGLE, PROTOCOL_JINGLE);
}


// Hybrid => Hybrid = Jingle (with other content)
TEST_F(SessionTest, HybridToHybridOtherContent) {
  TestOtherContent(PROTOCOL_HYBRID, PROTOCOL_HYBRID, PROTOCOL_JINGLE);
}

// Hybrid => Hybrid = Jingle (with audio content)
TEST_F(SessionTest, HybridToHybridAudioContent) {
  TestAudioContent(PROTOCOL_HYBRID, PROTOCOL_HYBRID, PROTOCOL_JINGLE);
}

// Hybrid => Hybrid = Jingle (with video contents)
TEST_F(SessionTest, HybridToHybridVideoContents) {
  TestVideoContents(PROTOCOL_HYBRID, PROTOCOL_HYBRID, PROTOCOL_JINGLE);
}


// Gingle => Hybrid = Gingle (with other content)
TEST_F(SessionTest, GingleToHybridOtherContent) {
  TestOtherContent(PROTOCOL_GINGLE, PROTOCOL_HYBRID, PROTOCOL_GINGLE);
}

// Gingle => Hybrid = Gingle (with audio content)
TEST_F(SessionTest, GingleToHybridAudioContent) {
  TestAudioContent(PROTOCOL_GINGLE, PROTOCOL_HYBRID, PROTOCOL_GINGLE);
}

// Gingle => Hybrid = Gingle (with video contents)
TEST_F(SessionTest, GingleToHybridVideoContents) {
  TestVideoContents(PROTOCOL_GINGLE, PROTOCOL_HYBRID, PROTOCOL_GINGLE);
}


// Jingle => Hybrid = Jingle (with other content)
TEST_F(SessionTest, JingleToHybridOtherContent) {
  TestOtherContent(PROTOCOL_JINGLE, PROTOCOL_HYBRID, PROTOCOL_JINGLE);
}

// Jingle => Hybrid = Jingle (with audio content)
TEST_F(SessionTest, JingleToHybridAudioContent) {
  TestAudioContent(PROTOCOL_JINGLE, PROTOCOL_HYBRID, PROTOCOL_JINGLE);
}

// Jingle => Hybrid = Jingle (with video contents)
TEST_F(SessionTest, JingleToHybridVideoContents) {
  TestVideoContents(PROTOCOL_JINGLE, PROTOCOL_HYBRID, PROTOCOL_JINGLE);
}


// Hybrid => Gingle = Gingle (with other content)
TEST_F(SessionTest, HybridToGingleOtherContent) {
  TestOtherContent(PROTOCOL_HYBRID, PROTOCOL_GINGLE, PROTOCOL_GINGLE);
}

// Hybrid => Gingle = Gingle (with audio content)
TEST_F(SessionTest, HybridToGingleAudioContent) {
  TestAudioContent(PROTOCOL_HYBRID, PROTOCOL_GINGLE, PROTOCOL_GINGLE);
}

// Hybrid => Gingle = Gingle (with video contents)
TEST_F(SessionTest, HybridToGingleVideoContents) {
  TestVideoContents(PROTOCOL_HYBRID, PROTOCOL_GINGLE, PROTOCOL_GINGLE);
}


// Hybrid => Jingle = Jingle (with other content)
TEST_F(SessionTest, HybridToJingleOtherContent) {
  TestOtherContent(PROTOCOL_HYBRID, PROTOCOL_JINGLE, PROTOCOL_JINGLE);
}

// Hybrid => Jingle = Jingle (with audio content)
TEST_F(SessionTest, HybridToJingleAudioContent) {
  TestAudioContent(PROTOCOL_HYBRID, PROTOCOL_JINGLE, PROTOCOL_JINGLE);
}

// Hybrid => Jingle = Jingle (with video contents)
TEST_F(SessionTest, HybridToJingleVideoContents) {
  TestVideoContents(PROTOCOL_HYBRID, PROTOCOL_JINGLE, PROTOCOL_JINGLE);
}


TEST_F(SessionTest, GingleEarlyTerminationFromInitiator) {
  TestEarlyTerminationFromInitiator(PROTOCOL_GINGLE);
}

TEST_F(SessionTest, JingleEarlyTerminationFromInitiator) {
  TestEarlyTerminationFromInitiator(PROTOCOL_JINGLE);
}

TEST_F(SessionTest, HybridEarlyTerminationFromInitiator) {
  TestEarlyTerminationFromInitiator(PROTOCOL_HYBRID);
}


TEST_F(SessionTest, GingleRejection) {
  TestRejection(PROTOCOL_GINGLE);
}

TEST_F(SessionTest, JingleRejection) {
  TestRejection(PROTOCOL_JINGLE);
}

TEST_F(SessionTest, GingleGoodRedirect) {
  TestGoodRedirect(PROTOCOL_GINGLE);
}

TEST_F(SessionTest, JingleGoodRedirect) {
  TestGoodRedirect(PROTOCOL_JINGLE);
}

TEST_F(SessionTest, GingleBadRedirect) {
  TestBadRedirect(PROTOCOL_GINGLE);
}

TEST_F(SessionTest, JingleBadRedirect) {
  TestBadRedirect(PROTOCOL_JINGLE);
}

TEST_F(SessionTest, TestCandidatesInInitiateAndAccept) {
  TestCandidatesInInitiateAndAccept("Candidates in initiate/accept");
}

TEST_F(SessionTest, TestTransportMux) {
  TestTransportMux();
}
