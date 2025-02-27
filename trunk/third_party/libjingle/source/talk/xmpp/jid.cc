/*
 * libjingle
 * Copyright 2004--2005, Google Inc.
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

#include "talk/xmpp/jid.h"

#include <ctype.h>

#include <algorithm>
#include <string>

#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/xmpp/constants.h"

namespace buzz {

Jid::Jid() : data_(NULL) {
}

Jid::Jid(bool is_special, const std::string & special) {
  data_ = is_special ? new Data(special, STR_EMPTY, STR_EMPTY) : NULL;
}

Jid::Jid(const std::string & jid_string) {
  if (jid_string.empty()) {
    data_ = NULL;
    return;
  }

  // First find the slash and slice off that part
  size_t slash = jid_string.find('/');
  std::string resource_name = (slash == std::string::npos ? STR_EMPTY :
                    jid_string.substr(slash + 1));

  // Now look for the node
  std::string node_name;
  size_t at = jid_string.find('@');
  size_t domain_begin;
  if (at < slash && at != std::string::npos) {
    node_name = jid_string.substr(0, at);
    domain_begin = at + 1;
  } else {
    domain_begin = 0;
  }

  // Now take what is left as the domain
  size_t domain_length =
    (  slash == std::string::npos
     ? jid_string.length() - domain_begin
     : slash - domain_begin);

  // avoid allocating these constants repeatedly
  std::string domain_name;

  if (domain_length == 9  && jid_string.find("gmail.com", domain_begin) == domain_begin) {
    domain_name = STR_GMAIL_COM;
  }
  else if (domain_length == 14 && jid_string.find("googlemail.com", domain_begin) == domain_begin) {
    domain_name = STR_GOOGLEMAIL_COM;
  }
  else if (domain_length == 10 && jid_string.find("google.com", domain_begin) == domain_begin) {
    domain_name = STR_GOOGLE_COM;
  }
  else {
    domain_name = jid_string.substr(domain_begin, domain_length);
  }

  // If the domain is empty we have a non-valid jid and we should empty
  // everything else out
  if (domain_name.empty()) {
    data_ = NULL;
    return;
  }

  bool valid_node;
  std::string validated_node = prepNode(node_name,
      node_name.begin(), node_name.end(), &valid_node);
  bool valid_domain;
  std::string validated_domain = prepDomain(domain_name,
      domain_name.begin(), domain_name.end(), &valid_domain);
  bool valid_resource;
  std::string validated_resource = prepResource(resource_name,
      resource_name.begin(), resource_name.end(), &valid_resource);

  if (!valid_node || !valid_domain || !valid_resource) {
    data_ = NULL;
    return;
  }

  data_ = new Data(validated_node, validated_domain, validated_resource);
}

Jid::Jid(const std::string & node_name,
         const std::string & domain_name,
         const std::string & resource_name) {
  if (domain_name.empty()) {
    data_ = NULL;
    return;
  }

  bool valid_node;
  std::string validated_node = prepNode(node_name,
      node_name.begin(), node_name.end(), &valid_node);
  bool valid_domain;
  std::string validated_domain = prepDomain(domain_name,
      domain_name.begin(), domain_name.end(), &valid_domain);
  bool valid_resource;
  std::string validated_resource = prepResource(resource_name,
      resource_name.begin(), resource_name.end(), &valid_resource);

  if (!valid_node || !valid_domain || !valid_resource) {
    data_ = NULL;
    return;
  }

  data_ = new Data(validated_node, validated_domain, validated_resource);
}

std::string Jid::Str() const {
  if (!IsValid())
    return STR_EMPTY;

  std::string ret;

  if (!data_->node_name_.empty())
    ret = data_->node_name_ + "@";

  ASSERT(data_->domain_name_ != STR_EMPTY);
  ret += data_->domain_name_;

  if (!data_->resource_name_.empty())
    ret += "/" + data_->resource_name_;

  return ret;
}

bool
Jid::IsEmpty() const {
  return data_ == NULL ||
      (data_->node_name_.empty() && data_->domain_name_.empty() &&
       data_->resource_name_.empty());
}

bool
Jid::IsValid() const {
  return data_ != NULL && !data_->domain_name_.empty();
}

bool
Jid::IsBare() const {
  if (IsEmpty()) {
    LOG(LS_VERBOSE) << "Warning: Calling IsBare() on the empty jid";
    return true;
  }
  return IsValid() &&
         data_->resource_name_.empty();
}

bool
Jid::IsFull() const {
  return IsValid() &&
         !data_->resource_name_.empty();
}

Jid
Jid::BareJid() const {
  if (!IsValid())
    return Jid();
  if (!IsFull())
    return *this;
  return Jid(data_->node_name_, data_->domain_name_, STR_EMPTY);
}

#if 0
void
Jid::set_node(const std::string & node_name) {
    data_->node_name_ = node_name;
}
void
Jid::set_domain(const std::string & domain_name) {
    data_->domain_name_ = domain_name;
}
void
Jid::set_resource(const std::string & res_name) {
    data_->resource_name_ = res_name;
}
#endif

bool
Jid::BareEquals(const Jid & other) const {
  return (other.data_ == data_ ||
          (data_ != NULL &&
          other.data_ != NULL &&
          other.data_->node_name_ == data_->node_name_ &&
          other.data_->domain_name_ == data_->domain_name_));
}

bool
Jid::operator==(const Jid & other) const {
  return (other.data_ == data_ ||
          (data_ != NULL &&
          other.data_ != NULL &&
          other.data_->node_name_ == data_->node_name_ &&
          other.data_->domain_name_ == data_->domain_name_ &&
          other.data_->resource_name_ == data_->resource_name_));
}

int
Jid::Compare(const Jid & other) const {
  if (other.data_ == data_)
    return 0;
  if (data_ == NULL)
    return -1;
  if (other.data_ == NULL)
    return 1;

  int compare_result;
  compare_result = data_->node_name_.compare(other.data_->node_name_);
  if (0 != compare_result)
    return compare_result;
  compare_result = data_->domain_name_.compare(other.data_->domain_name_);
  if (0 != compare_result)
    return compare_result;
  compare_result = data_->resource_name_.compare(other.data_->resource_name_);
  return compare_result;
}

uint32 Jid::ComputeLameHash() const {
  uint32 hash = 0;
  // Hash the node portion
  {
    const std::string &str = node();
    for (int i = 0; i < static_cast<int>(str.size()); ++i) {
      hash = ((hash << 2) + hash) + str[i];
    }
  }

  // Hash the domain portion
  {
    const std::string &str = domain();
    for (int i = 0; i < static_cast<int>(str.size()); ++i)
      hash = ((hash << 2) + hash) + str[i];
  }

  // Hash the resource portion
  {
    const std::string &str = resource();
    for (int i = 0; i < static_cast<int>(str.size()); ++i)
      hash = ((hash << 2) + hash) + str[i];
  }

  return hash;
}

// --- JID parsing code: ---

// Checks and normalizes the node part of a JID.
std::string
Jid::prepNode(const std::string str, std::string::const_iterator start,
    std::string::const_iterator end, bool *valid) {
  *valid = false;
  std::string result;

  for (std::string::const_iterator i = start; i < end; i++) {
    bool char_valid = true;
    unsigned char ch = *i;
    if (ch <= 0x7F) {
      result += prepNodeAscii(ch, &char_valid);
    }
    else {
      // TODO: implement the correct stringprep protocol for these
      result += tolower(ch);
    }
    if (!char_valid) {
      return STR_EMPTY;
    }
  }

  if (result.length() > 1023) {
    return STR_EMPTY;
  }
  *valid = true;
  return result;
}


// Returns the appropriate mapping for an ASCII character in a node.
char
Jid::prepNodeAscii(char ch, bool *valid) {
  *valid = true;
  switch (ch) {
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
    case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
    case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
    case 'V': case 'W': case 'X': case 'Y': case 'Z':
      return (char)(ch + ('a' - 'A'));

    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11:
    case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
    case ' ': case '&': case '/': case ':': case '<': case '>': case '@':
    case '\"': case '\'':
    case 0x7F:
      *valid = false;
      return 0;

    default:
      return ch;
  }
}


// Checks and normalizes the resource part of a JID.
std::string
Jid::prepResource(const std::string str, std::string::const_iterator start,
    std::string::const_iterator end, bool *valid) {
  *valid = false;
  std::string result;

  for (std::string::const_iterator i = start; i < end; i++) {
    bool char_valid = true;
    unsigned char ch = *i;
    if (ch <= 0x7F) {
      result += prepResourceAscii(ch, &char_valid);
    }
    else {
      // TODO: implement the correct stringprep protocol for these
      result += ch;
    }
  }

  if (result.length() > 1023) {
    return STR_EMPTY;
  }
  *valid = true;
  return result;
}

// Returns the appropriate mapping for an ASCII character in a resource.
char
Jid::prepResourceAscii(char ch, bool *valid) {
  *valid = true;
  switch (ch) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11:
    case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x7F:
      *valid = false;
      return 0;

    default:
      return ch;
  }
}

// Checks and normalizes the domain part of a JID.
std::string
Jid::prepDomain(const std::string str, std::string::const_iterator start,
    std::string::const_iterator end, bool *valid) {
  *valid = false;
  std::string result;

  // TODO: if the domain contains a ':', then we should parse it
  // as an IPv6 address rather than giving an error about illegal domain.
  prepDomain(str, start, end, &result, valid);
  if (!*valid) {
    return STR_EMPTY;
  }

  if (result.length() > 1023) {
    return STR_EMPTY;
  }
  *valid = true;
  return result;
}


// Checks and normalizes an IDNA domain.
void
Jid::prepDomain(const std::string str, std::string::const_iterator start,
    std::string::const_iterator end, std::string *buf, bool *valid) {
  *valid = false;
  std::string::const_iterator last = start;
  for (std::string::const_iterator i = start; i < end; i++) {
    bool label_valid = true;
    char ch = *i;
    switch (ch) {
      case 0x002E:
#if 0 // FIX: This isn't UTF-8-aware.
      case 0x3002:
      case 0xFF0E:
      case 0xFF61:
#endif
        prepDomainLabel(str, last, i, buf, &label_valid);
        *buf += '.';
        last = i + 1;
        break;
    }
    if (!label_valid) {
      return;
    }
  }
  prepDomainLabel(str, last, end, buf, valid);
}

// Checks and normalizes a domain label.
void
Jid::prepDomainLabel(const std::string str, std::string::const_iterator start,
    std::string::const_iterator end, std::string *buf, bool *valid) {
  *valid = false;

  int startLen = buf->length();
  for (std::string::const_iterator i = start; i < end; i++) {
    bool char_valid = true;
    unsigned char ch = *i;
    if (ch <= 0x7F) {
      *buf += prepDomainLabelAscii(ch, &char_valid);
    }
    else {
      // TODO: implement ToASCII for these
      *buf += ch;
    }
    if (!char_valid) {
      return;
    }
  }

  int count = buf->length() - startLen;
  if (count == 0) {
    return;
  }
  else if (count > 63) {
    return;
  }

  // Is this check needed? See comment in prepDomainLabelAscii.
  if ((*buf)[startLen] == '-') {
    return;
  }
  if ((*buf)[buf->length() - 1] == '-') {
    return;
  }
  *valid = true;
}


// Returns the appropriate mapping for an ASCII character in a domain label.
char
Jid::prepDomainLabelAscii(char ch, bool *valid) {
  *valid = true;
  // TODO: A literal reading of the spec seems to say that we do
  // not need to check for these illegal characters (an "internationalized
  // domain label" runs ToASCII with UseSTD3... set to false).  But that
  // can't be right.  We should at least be checking that there are no '/'
  // or '@' characters in the domain.  Perhaps we should see what others
  // do in this case.

  switch (ch) {
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
    case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
    case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
    case 'V': case 'W': case 'X': case 'Y': case 'Z':
      return (char)(ch + ('a' - 'A'));

    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11:
    case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D:
    case 0x1E: case 0x1F: case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29:
    case 0x2A: case 0x2B: case 0x2C: case 0x2E: case 0x2F: case 0x3A:
    case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x40:
    case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: case 0x60:
    case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
      *valid = false;
      return 0;

    default:
      return ch;
  }
}

}
