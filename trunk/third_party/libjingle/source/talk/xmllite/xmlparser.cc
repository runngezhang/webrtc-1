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

#include "talk/xmllite/xmlparser.h"

#include <string>
#include <vector>

#include "talk/base/common.h"
#include "talk/xmllite/xmlconstants.h"
#include "talk/xmllite/xmlelement.h"
#include "talk/xmllite/xmlnsstack.h"
#include "talk/xmllite/xmlnsstack.h"

namespace buzz {


static void
StartElementCallback(void * userData, const char *name, const char **atts) {
  (static_cast<XmlParser *>(userData))->ExpatStartElement(name, atts);
}

static void
EndElementCallback(void * userData, const char *name) {
  (static_cast<XmlParser *>(userData))->ExpatEndElement(name);
}

static void
CharacterDataCallback(void * userData, const char *text, int len) {
  (static_cast<XmlParser *>(userData))->ExpatCharacterData(text, len);
}

static void
XmlDeclCallback(void * userData, const char * ver, const char * enc, int st) {
  (static_cast<XmlParser *>(userData))->ExpatXmlDecl(ver, enc, st);
}

XmlParser::XmlParser(XmlParseHandler *pxph) :
    context_(this), pxph_(pxph), sentError_(false) {
  expat_ = XML_ParserCreate(NULL);
  XML_SetUserData(expat_, this);
  XML_SetElementHandler(expat_, StartElementCallback, EndElementCallback);
  XML_SetCharacterDataHandler(expat_, CharacterDataCallback);
  XML_SetXmlDeclHandler(expat_, XmlDeclCallback);
}

void
XmlParser::Reset() {
  if (!XML_ParserReset(expat_, NULL)) {
    XML_ParserFree(expat_);
    expat_ = XML_ParserCreate(NULL);
  }
  XML_SetUserData(expat_, this);
  XML_SetElementHandler(expat_, StartElementCallback, EndElementCallback);
  XML_SetCharacterDataHandler(expat_, CharacterDataCallback);
  XML_SetXmlDeclHandler(expat_, XmlDeclCallback);
  context_.Reset();
  sentError_ = false;
}

static bool
XmlParser_StartsWithXmlns(const char *name) {
  return name[0] == 'x' &&
         name[1] == 'm' &&
         name[2] == 'l' &&
         name[3] == 'n' &&
         name[4] == 's';
}

void
XmlParser::ExpatStartElement(const char *name, const char **atts) {
  if (context_.RaisedError() != XML_ERROR_NONE)
    return;
  const char **att;
  context_.StartElement();
  for (att = atts; *att; att += 2) {
    if (XmlParser_StartsWithXmlns(*att)) {
      if ((*att)[5] == '\0') {
        context_.StartNamespace("", *(att + 1));
      }
      else if ((*att)[5] == ':') {
        if (**(att + 1) == '\0') {
          // In XML 1.0 empty namespace illegal with prefix (not in 1.1)
          context_.RaiseError(XML_ERROR_SYNTAX);
          return;
        }
        context_.StartNamespace((*att) + 6, *(att + 1));
      }
    }
  }
  context_.SetPosition(XML_GetCurrentLineNumber(expat_),
                       XML_GetCurrentColumnNumber(expat_),
                       XML_GetCurrentByteIndex(expat_));
  pxph_->StartElement(&context_, name, atts);
}

void
XmlParser::ExpatEndElement(const char *name) {
  if (context_.RaisedError() != XML_ERROR_NONE)
    return;
  context_.EndElement();
  context_.SetPosition(XML_GetCurrentLineNumber(expat_),
                       XML_GetCurrentColumnNumber(expat_),
                       XML_GetCurrentByteIndex(expat_));
  pxph_->EndElement(&context_, name);
}

void
XmlParser::ExpatCharacterData(const char *text, int len) {
  if (context_.RaisedError() != XML_ERROR_NONE)
    return;
  context_.SetPosition(XML_GetCurrentLineNumber(expat_),
                       XML_GetCurrentColumnNumber(expat_),
                       XML_GetCurrentByteIndex(expat_));
  pxph_->CharacterData(&context_, text, len);
}

void
XmlParser::ExpatXmlDecl(const char * ver, const char * enc, int standalone) {
  if (context_.RaisedError() != XML_ERROR_NONE)
    return;

  if (ver && std::string("1.0") != ver) {
    context_.RaiseError(XML_ERROR_SYNTAX);
    return;
  }

  if (standalone == 0) {
    context_.RaiseError(XML_ERROR_SYNTAX);
    return;
  }

  if (enc && !((enc[0] == 'U' || enc[0] == 'u') &&
               (enc[1] == 'T' || enc[1] == 't') &&
               (enc[2] == 'F' || enc[2] == 'f') &&
                enc[3] == '-' && enc[4] =='8')) {
    context_.RaiseError(XML_ERROR_INCORRECT_ENCODING);
    return;
  }

}

bool
XmlParser::Parse(const char *data, size_t len, bool isFinal) {
  if (sentError_)
    return false;

  if (XML_Parse(expat_, data, static_cast<int>(len), isFinal) !=
      XML_STATUS_OK) {
    context_.SetPosition(XML_GetCurrentLineNumber(expat_),
                         XML_GetCurrentColumnNumber(expat_),
                         XML_GetCurrentByteIndex(expat_));
    context_.RaiseError(XML_GetErrorCode(expat_));
  }

  if (context_.RaisedError() != XML_ERROR_NONE) {
    sentError_ = true;
    pxph_->Error(&context_, context_.RaisedError());
    return false;
  }

  return true;
}

XmlParser::~XmlParser() {
  XML_ParserFree(expat_);
}

void
XmlParser::ParseXml(XmlParseHandler *pxph, std::string text) {
  XmlParser parser(pxph);
  parser.Parse(text.c_str(), text.length(), true);
}

XmlParser::ParseContext::ParseContext(XmlParser *parser) :
    parser_(parser),
    xmlnsstack_(),
    raised_(XML_ERROR_NONE),
    line_number_(0),
    column_number_(0),
    byte_index_(0) {
}

void
XmlParser::ParseContext::StartNamespace(const char *prefix, const char *ns) {
  xmlnsstack_.AddXmlns(*prefix ? prefix : STR_EMPTY, ns);
}

void
XmlParser::ParseContext::StartElement() {
  xmlnsstack_.PushFrame();
}

void
XmlParser::ParseContext::EndElement() {
  xmlnsstack_.PopFrame();
}

QName
XmlParser::ParseContext::ResolveQName(const char* qname, bool isAttr) {
  const char *c;
  for (c = qname; *c; ++c) {
    if (*c == ':') {
      const std::pair<std::string, bool> result =
          xmlnsstack_.NsForPrefix(std::string(qname, c - qname));
      if (!result.second)
        return QN_EMPTY;
      return QName(result.first, c + 1);
    }
  }
  if (isAttr)
    return QName(STR_EMPTY, qname);

  std::pair<std::string, bool> result = xmlnsstack_.NsForPrefix(STR_EMPTY);
  if (!result.second)
    return QN_EMPTY;

  return QName(result.first, qname);
}

void
XmlParser::ParseContext::Reset() {
  xmlnsstack_.Reset();
  raised_ = XML_ERROR_NONE;
}

void
XmlParser::ParseContext::SetPosition(int line, int column,
                                          long byte_index) {
  line_number_ = line;
  column_number_ = column;
  byte_index_ = byte_index;
}

void
XmlParser::ParseContext::GetPosition(unsigned long * line,
                                     unsigned long * column,
                                     unsigned long * byte_index) {
  if (line != NULL) {
    *line = static_cast<unsigned long>(line_number_);
  }

  if (column != NULL) {
    *column = static_cast<unsigned long>(column_number_);
  }

  if (byte_index != NULL) {
    *byte_index = static_cast<unsigned long>(byte_index_);
  }
}

XmlParser::ParseContext::~ParseContext() {
}

}
