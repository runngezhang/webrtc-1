/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "peerconnection/samples/client/peer_connection_client.h"

#include "peerconnection/samples/client/defaults.h"
#include "talk/base/common.h"
#include "talk/base/nethelpers.h"
#include "talk/base/logging.h"
#include "talk/base/stringutils.h"

#ifdef WIN32
#include "talk/base/win32socketserver.h"
#endif

using talk_base::sprintfn;

namespace {

// This is our magical hangup signal.
const char kByeMessage[] = "BYE";

talk_base::AsyncSocket* CreateClientSocket() {
#ifdef WIN32
  return new talk_base::Win32Socket();
#elif defined(POSIX)
  talk_base::Thread* thread = talk_base::Thread::Current();
  ASSERT(thread != NULL);
  return thread->socketserver()->CreateAsyncSocket(SOCK_STREAM);
#else
#error Platform not supported.
#endif
}

}

PeerConnectionClient::PeerConnectionClient()
  : callback_(NULL),
    control_socket_(CreateClientSocket()),
    hanging_get_(CreateClientSocket()),
    state_(NOT_CONNECTED),
    my_id_(-1) {
  control_socket_->SignalCloseEvent.connect(this,
      &PeerConnectionClient::OnClose);
  hanging_get_->SignalCloseEvent.connect(this,
      &PeerConnectionClient::OnClose);
  control_socket_->SignalConnectEvent.connect(this,
      &PeerConnectionClient::OnConnect);
  hanging_get_->SignalConnectEvent.connect(this,
      &PeerConnectionClient::OnHangingGetConnect);
  control_socket_->SignalReadEvent.connect(this,
      &PeerConnectionClient::OnRead);
  hanging_get_->SignalReadEvent.connect(this,
      &PeerConnectionClient::OnHangingGetRead);
}

PeerConnectionClient::~PeerConnectionClient() {
}

int PeerConnectionClient::id() const {
  return my_id_;
}

bool PeerConnectionClient::is_connected() const {
  return my_id_ != -1;
}

const Peers& PeerConnectionClient::peers() const {
  return peers_;
}

void PeerConnectionClient::RegisterObserver(
    PeerConnectionClientObserver* callback) {
  ASSERT(!callback_);
  callback_ = callback;
}

bool PeerConnectionClient::Connect(const std::string& server, int port,
                                   const std::string& client_name) {
  ASSERT(!server.empty());
  ASSERT(!client_name.empty());

  if (state_ != NOT_CONNECTED) {
    LOG(WARNING)
        << "The client must not be connected before you can call Connect()";
    return false;
  }

  if (server.empty() || client_name.empty())
    return false;

  if (port <= 0)
    port = kDefaultServerPort;

  server_address_.SetIP(server);
  server_address_.SetPort(port);

  if (server_address_.IsUnresolved()) {
    int errcode = 0;
    hostent* h = talk_base::SafeGetHostByName(
          server_address_.IPAsString().c_str(), &errcode);
    if (!h) {
      LOG(LS_ERROR) << "Failed to resolve host name: "
                    << server_address_.IPAsString();
      return false;
    } else {
      server_address_.SetResolvedIP(
          ntohl(*reinterpret_cast<uint32*>(h->h_addr_list[0])));
      talk_base::FreeHostEnt(h);
    }
  }

  char buffer[1024];
  sprintfn(buffer, sizeof(buffer),
           "GET /sign_in?%s HTTP/1.0\r\n\r\n", client_name.c_str());
  onconnect_data_ = buffer;

  bool ret = ConnectControlSocket();
  if (ret)
    state_ = SIGNING_IN;

  return ret;
}

bool PeerConnectionClient::SendToPeer(int peer_id, const std::string& message) {
  if (state_ != CONNECTED)
    return false;

  ASSERT(is_connected());
  ASSERT(control_socket_->GetState() == talk_base::Socket::CS_CLOSED);
  if (!is_connected() || peer_id == -1)
    return false;

  char headers[1024];
  sprintfn(headers, sizeof(headers),
      "POST /message?peer_id=%i&to=%i HTTP/1.0\r\n"
      "Content-Length: %i\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n",
      my_id_, peer_id, message.length());
  onconnect_data_ = headers;
  onconnect_data_ += message;
  return ConnectControlSocket();
}

bool PeerConnectionClient::SendHangUp(int peer_id) {
  return SendToPeer(peer_id, kByeMessage);
}

bool PeerConnectionClient::IsSendingMessage() {
  return state_ == CONNECTED &&
         control_socket_->GetState() != talk_base::Socket::CS_CLOSED;
}

bool PeerConnectionClient::SignOut() {
  if (state_ == NOT_CONNECTED || state_ == SIGNING_OUT)
    return true;

  if (hanging_get_->GetState() != talk_base::Socket::CS_CLOSED)
    hanging_get_->Close();

  if (control_socket_->GetState() == talk_base::Socket::CS_CLOSED) {
    state_ = SIGNING_OUT;

    if (my_id_ != -1) {
      char buffer[1024];
      sprintfn(buffer, sizeof(buffer),
          "GET /sign_out?peer_id=%i HTTP/1.0\r\n\r\n", my_id_);
      onconnect_data_ = buffer;
      return ConnectControlSocket();
    } else {
      // Can occur if the app is closed before we finish connecting.
      return true;
    }
  } else {
    state_ = SIGNING_OUT_WAITING;
  }

  return true;
}

void PeerConnectionClient::Close() {
  control_socket_->Close();
  hanging_get_->Close();
  onconnect_data_.clear();
  peers_.clear();
  my_id_ = -1;
  state_ = NOT_CONNECTED;
}

bool PeerConnectionClient::ConnectControlSocket() {
  ASSERT(control_socket_->GetState() == talk_base::Socket::CS_CLOSED);
  int err = control_socket_->Connect(server_address_);
  if (err == SOCKET_ERROR) {
    Close();
    return false;
  }
  return true;
}

void PeerConnectionClient::OnConnect(talk_base::AsyncSocket* socket) {
  ASSERT(!onconnect_data_.empty());
  size_t sent = socket->Send(onconnect_data_.c_str(), onconnect_data_.length());
  ASSERT(sent == onconnect_data_.length());
  UNUSED(sent);
  onconnect_data_.clear();
  LOG(WARNING)
        << "nish OnConnect()";
}

void PeerConnectionClient::OnHangingGetConnect(talk_base::AsyncSocket* socket) {
  char buffer[1024];
  sprintfn(buffer, sizeof(buffer),
           "GET /wait?peer_id=%i HTTP/1.0\r\n\r\n", my_id_);
  int len = strlen(buffer);
  int sent = socket->Send(buffer, len);
  ASSERT(sent == len);
  UNUSED2(sent, len);
}

void PeerConnectionClient::OnMessageFromPeer(int peer_id,
                                             const std::string& message) {
  if (message.length() == (sizeof(kByeMessage) - 1) &&
      message.compare(kByeMessage) == 0) {
    callback_->OnPeerDisconnected(peer_id);
  } else {
    callback_->OnMessageFromPeer(peer_id, message);
  }
}

bool PeerConnectionClient::GetHeaderValue(const std::string& data,
                                          size_t eoh,
                                          const char* header_pattern,
                                          size_t* value) {
  ASSERT(value != NULL);
  size_t found = data.find(header_pattern);
  if (found != std::string::npos && found < eoh) {
    *value = atoi(&data[found + strlen(header_pattern)]);
    return true;
  }
  return false;
}

bool PeerConnectionClient::GetHeaderValue(const std::string& data, size_t eoh,
                                          const char* header_pattern,
                                          std::string* value) {
  ASSERT(value != NULL);
  size_t found = data.find(header_pattern);
  if (found != std::string::npos && found < eoh) {
    size_t begin = found + strlen(header_pattern);
    size_t end = data.find("\r\n", begin);
    if (end == std::string::npos)
      end = eoh;
    value->assign(data.substr(begin, end - begin));
    return true;
  }
  return false;
}

bool PeerConnectionClient::ReadIntoBuffer(talk_base::AsyncSocket* socket,
                                          std::string* data,
                                          size_t* content_length) {
  LOG(INFO) << __FUNCTION__;

  char buffer[0xffff];
  do {
    int bytes = socket->Recv(buffer, sizeof(buffer));
    if (bytes <= 0)
      break;
    data->append(buffer, bytes);
  } while (true);

  bool ret = false;
  size_t i = data->find("\r\n\r\n");
  if (i != std::string::npos) {
    LOG(INFO) << "Headers received";
    if (GetHeaderValue(*data, i, "\r\nContent-Length: ", content_length)) {
      LOG(INFO) << "Expecting " << *content_length << " bytes.";
      size_t total_response_size = (i + 4) + *content_length;
      if (data->length() >= total_response_size) {
        ret = true;
        std::string should_close;
        const char kConnection[] = "\r\nConnection: ";
        if (GetHeaderValue(*data, i, kConnection, &should_close) &&
            should_close.compare("close") == 0) {
          socket->Close();
          // Since we closed the socket, there was no notification delivered
          // to us.  Compensate by letting ourselves know.
          OnClose(socket, 0);
        }
      } else {
        // We haven't received everything.  Just continue to accept data.
      }
    } else {
      LOG(LS_ERROR) << "No content length field specified by the server.";
    }
  }
  return ret;
}

void PeerConnectionClient::OnRead(talk_base::AsyncSocket* socket) {
  LOG(INFO) << __FUNCTION__;
  size_t content_length = 0;
  if (ReadIntoBuffer(socket, &control_data_, &content_length)) {
    size_t peer_id = 0, eoh = 0;
    bool ok = ParseServerResponse(control_data_, content_length, &peer_id,
                                  &eoh);
    if (ok) {
      if (my_id_ == -1) {
        // First response.  Let's store our server assigned ID.
        ASSERT(state_ == SIGNING_IN);
        my_id_ = peer_id;
        ASSERT(my_id_ != -1);

        // The body of the response will be a list of already connected peers.
        if (content_length) {
          size_t pos = eoh + 4;
          while (pos < control_data_.size()) {
            size_t eol = control_data_.find('\n', pos);
            if (eol == std::string::npos)
              break;
            int id = 0;
            std::string name;
            bool connected;
            if (ParseEntry(control_data_.substr(pos, eol - pos), &name, &id,
                           &connected) && id != my_id_) {
              peers_[id] = name;
              callback_->OnPeerConnected(id, name);
            }
            pos = eol + 1;
          }
        }
        ASSERT(is_connected());
        callback_->OnSignedIn();
      } else if (state_ == SIGNING_OUT) {
        Close();
        callback_->OnDisconnected();
      } else if (state_ == SIGNING_OUT_WAITING) {
        SignOut();
      }
    }

    control_data_.clear();

    if (state_ == SIGNING_IN) {
      ASSERT(hanging_get_->GetState() == talk_base::Socket::CS_CLOSED);
      state_ = CONNECTED;
      hanging_get_->Connect(server_address_);
    }
  }
}

void PeerConnectionClient::OnHangingGetRead(talk_base::AsyncSocket* socket) {
  LOG(INFO) << __FUNCTION__;
  size_t content_length = 0;
  if (ReadIntoBuffer(socket, &notification_data_, &content_length)) {
    size_t peer_id = 0, eoh = 0;
    bool ok = ParseServerResponse(notification_data_, content_length,
                                  &peer_id, &eoh);

    if (ok) {
      // Store the position where the body begins.
      size_t pos = eoh + 4;

      if (my_id_ == static_cast<int>(peer_id)) {
        // A notification about a new member or a member that just
        // disconnected.
        int id = 0;
        std::string name;
        bool connected = false;
        if (ParseEntry(notification_data_.substr(pos), &name, &id,
                       &connected)) {
          if (connected) {
            peers_[id] = name;
            callback_->OnPeerConnected(id, name);
          } else {
            peers_.erase(id);
            callback_->OnPeerDisconnected(id);
          }
        }
      } else {
        OnMessageFromPeer(peer_id, notification_data_.substr(pos));
      }
    }

    notification_data_.clear();
  }

  if (hanging_get_->GetState() == talk_base::Socket::CS_CLOSED &&
      state_ == CONNECTED) {
    hanging_get_->Connect(server_address_);
  }
}

bool PeerConnectionClient::ParseEntry(const std::string& entry,
                                      std::string* name,
                                      int* id,
                                      bool* connected) {
  ASSERT(name != NULL);
  ASSERT(id != NULL);
  ASSERT(connected != NULL);
  ASSERT(!entry.empty());

  *connected = false;
  size_t separator = entry.find(',');
  if (separator != std::string::npos) {
    *id = atoi(&entry[separator + 1]);
    name->assign(entry.substr(0, separator));
    separator = entry.find(',', separator + 1);
    if (separator != std::string::npos) {
      *connected = atoi(&entry[separator + 1]) ? true : false;
    }
  }
  return !name->empty();
}

int PeerConnectionClient::GetResponseStatus(const std::string& response) {
  int status = -1;
  size_t pos = response.find(' ');
  if (pos != std::string::npos)
    status = atoi(&response[pos + 1]);
  return status;
}

bool PeerConnectionClient::ParseServerResponse(const std::string& response,
                                               size_t content_length,
                                               size_t* peer_id,
                                               size_t* eoh) {
  LOG(INFO) << response;

  int status = GetResponseStatus(response.c_str());
  if (status != 200) {
    LOG(LS_ERROR) << "Received error from server";
    Close();
    callback_->OnDisconnected();
    return false;
  }

  *eoh = response.find("\r\n\r\n");
  ASSERT(*eoh != std::string::npos);
  if (*eoh == std::string::npos)
    return false;

  *peer_id = -1;

  // See comment in peer_channel.cc for why we use the Pragma header and
  // not e.g. "X-Peer-Id".
  GetHeaderValue(response, *eoh, "\r\nPragma: ", peer_id);

  return true;
}

void PeerConnectionClient::OnClose(talk_base::AsyncSocket* socket, int err) {
  LOG(INFO) << __FUNCTION__;

  socket->Close();

#ifdef WIN32
  if (err != WSAECONNREFUSED) {
#else
  if (err != ECONNREFUSED) {
#endif
    if (socket == hanging_get_.get()) {
      if (state_ == CONNECTED) {
        LOG(INFO) << "Issuing  a new hanging get";
        hanging_get_->Close();
        hanging_get_->Connect(server_address_);
      }
    } else {
      callback_->OnMessageSent(err);
    }
  } else {
    LOG(WARNING) << "Failed to connect to the server";
    Close();
    callback_->OnDisconnected();
  }
}
