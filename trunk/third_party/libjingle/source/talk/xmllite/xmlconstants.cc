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

#include "talk/xmllite/xmlconstants.h"

namespace buzz {

const char STR_EMPTY[] = "";
const char NS_XML[] = "http://www.w3.org/XML/1998/namespace";
const char NS_XMLNS[] = "http://www.w3.org/2000/xmlns/";
const char STR_XMLNS[] = "xmlns";
const char STR_XML[] = "xml";
const char STR_VERSION[] = "version";
const char STR_ENCODING[] = "encoding";

const StaticQName QN_EMPTY = { STR_EMPTY, STR_EMPTY };
const StaticQName QN_XMLNS = { STR_EMPTY, STR_XMLNS };

// TODO: Local statics are not thread-safe. Remove the
// following two functions if possible.
const std::string& EmptyStringRef() {
  static std::string result;
  return result;
}

const QName& EmptyQNameRef() {
  static QName result(QN_EMPTY);
  return result;
}


}
