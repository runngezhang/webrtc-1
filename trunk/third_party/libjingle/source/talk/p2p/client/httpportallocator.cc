/*
 * libjingle
 * Copyright 2004--2008, Google Inc.
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

#include "talk/p2p/client/httpportallocator.h"

#include <map>

#include "talk/base/asynchttprequest.h"
#include "talk/base/basicdefs.h"
#include "talk/base/common.h"
#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/nethelpers.h"
#include "talk/base/signalthread.h"

namespace {

const uint32 MSG_TIMEOUT = 100;  // must not conflict
  // with BasicPortAllocator.cpp

// Helper routine to remove whitespace from the ends of a string.
void Trim(std::string& str) {
  size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    str.clear();
    return;
  }

  ASSERT(str.find_last_not_of(" \t\r\n") != std::string::npos);
}

// Parses the lines in the result of the HTTP request that are of the form
// 'a=b' and returns them in a map.
typedef std::map<std::string, std::string> StringMap;
void ParseMap(const std::string& string, StringMap& map) {
  size_t start_of_line = 0;
  size_t end_of_line = 0;

  for (;;) {  // for each line
    start_of_line = string.find_first_not_of("\r\n", end_of_line);
    if (start_of_line == std::string::npos)
      break;

    end_of_line = string.find_first_of("\r\n", start_of_line);
    if (end_of_line == std::string::npos) {
      end_of_line = string.length();
    }

    size_t equals = string.find('=', start_of_line);
    if ((equals >= end_of_line) || (equals == std::string::npos))
      continue;

    std::string key(string, start_of_line, equals - start_of_line);
    std::string value(string, equals + 1, end_of_line - equals - 1);

    Trim(key);
    Trim(value);

    if ((key.size() > 0) && (value.size() > 0))
      map[key] = value;
  }
}

}  // namespace

namespace cricket {

// HttpPortAllocator

const int HttpPortAllocator::kNumRetries = 5;

const char HttpPortAllocator::kCreateSessionURL[] = "/create_session";

HttpPortAllocator::HttpPortAllocator(
    talk_base::NetworkManager* network_manager,
    talk_base::PacketSocketFactory* socket_factory,
    const std::string &user_agent)
    : BasicPortAllocator(network_manager, socket_factory), agent_(user_agent) {
  relay_hosts_.push_back("relay.google.com");
  stun_hosts_.push_back(
      talk_base::SocketAddress("stun.l.google.com", 19302));
}

HttpPortAllocator::HttpPortAllocator(
    talk_base::NetworkManager* network_manager,
    const std::string &user_agent)
    : BasicPortAllocator(network_manager), agent_(user_agent) {
  relay_hosts_.push_back("relay.google.com");
  stun_hosts_.push_back(
      talk_base::SocketAddress("stun.l.google.com", 19302));
}

HttpPortAllocator::~HttpPortAllocator() {
}

PortAllocatorSession *HttpPortAllocator::CreateSession(
    const std::string& name, const std::string& session_type) {
  return new HttpPortAllocatorSession(this, name, session_type, stun_hosts_,
      relay_hosts_, relay_token_, agent_);
}

// HttpPortAllocatorSession

HttpPortAllocatorSession::HttpPortAllocatorSession(
    HttpPortAllocator* allocator, const std::string &name,
    const std::string& session_type,
    const std::vector<talk_base::SocketAddress>& stun_hosts,
    const std::vector<std::string>& relay_hosts,
    const std::string& relay_token,
    const std::string& user_agent)
    : BasicPortAllocatorSession(allocator, name, session_type),
      relay_hosts_(relay_hosts), stun_hosts_(stun_hosts),
      relay_token_(relay_token), agent_(user_agent), attempts_(0) {
}

void HttpPortAllocatorSession::GetPortConfigurations() {
  // Creating relay sessions can take time and is done asynchronously.
  // Creating stun sessions could also take time and could be done aysnc also,
  // but for now is done here and added to the initial config.  Note any later
  // configs will have unresolved stun ips and will be discarded by the
  // AllocationSequence.
  PortConfiguration* config = new PortConfiguration(stun_hosts_[0], "", "", "");
  ConfigReady(config);
  TryCreateRelaySession();
}

void HttpPortAllocatorSession::TryCreateRelaySession() {
  if (allocator()->flags() & PORTALLOCATOR_DISABLE_RELAY) {
    LOG(LS_VERBOSE) << "HttpPortAllocator: Relay ports disabled, skipping.";
    return;
  }

  if (attempts_ == HttpPortAllocator::kNumRetries) {
    LOG(LS_ERROR) << "HttpPortAllocator: maximum number of requests reached; "
                  << "giving up on relay.";
    return;
  }

  if (relay_hosts_.size() == 0) {
    LOG(LS_ERROR) << "HttpPortAllocator: no relay hosts configured.";
    return;
  }

  // Choose the next host to try.
  std::string host = relay_hosts_[attempts_ % relay_hosts_.size()];
  attempts_++;
  LOG(LS_INFO) << "HTTPPortAllocator: sending to relay host " << host;
  if (relay_token_.empty()) {
    LOG(LS_WARNING) << "No relay auth token found.";
  }

  SendSessionRequest(host, talk_base::HTTP_SECURE_PORT);
}

void HttpPortAllocatorSession::SendSessionRequest(const std::string& host,
                                                  int port) {
  // Initiate an HTTP request to create a session through the chosen host.
  talk_base::AsyncHttpRequest* request =
      new talk_base::AsyncHttpRequest(agent_);
  request->SignalWorkDone.connect(this,
      &HttpPortAllocatorSession::OnRequestDone);

  request->set_secure(port == talk_base::HTTP_SECURE_PORT);
  request->set_proxy(allocator()->proxy());
  request->response().document.reset(new talk_base::MemoryStream);
  request->request().verb = talk_base::HV_GET;
  request->request().path = HttpPortAllocator::kCreateSessionURL;
  request->request().addHeader("X-Talk-Google-Relay-Auth", relay_token_, true);
  request->request().addHeader("X-Google-Relay-Auth", relay_token_, true);
  request->request().addHeader("X-Session-Type", session_type(), true);
  request->request().addHeader("X-Stream-Type", name(), true);
  request->set_host(host);
  request->set_port(port);
  request->Start();
  request->Release();
}

void HttpPortAllocatorSession::OnRequestDone(talk_base::SignalThread* data) {
  talk_base::AsyncHttpRequest* request =
      static_cast<talk_base::AsyncHttpRequest*>(data);
  if (request->response().scode != 200) {
    LOG(LS_WARNING) << "HTTPPortAllocator: request "
                    << " received error " << request->response().scode;
    TryCreateRelaySession();
    return;
  }
  LOG(LS_INFO) << "HTTPPortAllocator: request succeeded";

  talk_base::MemoryStream* stream =
      static_cast<talk_base::MemoryStream*>(request->response().document.get());
  stream->Rewind();
  size_t length;
  stream->GetSize(&length);
  std::string resp = std::string(stream->GetBuffer(), length);
  ReceiveSessionResponse(resp);
}

void HttpPortAllocatorSession::ReceiveSessionResponse(
    const std::string& response) {

  StringMap map;
  ParseMap(response, map);

  std::string username = map["username"];
  std::string password = map["password"];
  std::string magic_cookie = map["magic_cookie"];

  std::string relay_ip = map["relay.ip"];
  std::string relay_udp_port = map["relay.udp_port"];
  std::string relay_tcp_port = map["relay.tcp_port"];
  std::string relay_ssltcp_port = map["relay.ssltcp_port"];

  PortConfiguration* config = new PortConfiguration(stun_hosts_[0],
                                                    username,
                                                    password,
                                                    magic_cookie);

  PortConfiguration::PortList ports;
  if (!relay_udp_port.empty()) {
    talk_base::SocketAddress address(relay_ip, atoi(relay_udp_port.c_str()));
    ports.push_back(ProtocolAddress(address, PROTO_UDP));
  }
  if (!relay_tcp_port.empty()) {
    talk_base::SocketAddress address(relay_ip, atoi(relay_tcp_port.c_str()));
    ports.push_back(ProtocolAddress(address, PROTO_TCP));
  }
  if (!relay_ssltcp_port.empty()) {
    talk_base::SocketAddress address(relay_ip, atoi(relay_ssltcp_port.c_str()));
    ports.push_back(ProtocolAddress(address, PROTO_SSLTCP));
  }
  config->AddRelay(ports, 0.0f);
  ConfigReady(config);
}

}  // namespace cricket
