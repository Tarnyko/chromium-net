// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/server/http_server_request_info.h"

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace net {

HttpServerRequestInfo::HttpServerRequestInfo() {}

HttpServerRequestInfo::~HttpServerRequestInfo() {}

std::string HttpServerRequestInfo::GetHeaderValue(
    const std::string& header_name) const {
  DCHECK_EQ(base::ToLowerASCII(header_name), header_name);
  HttpServerRequestInfo::HeadersMap::const_iterator it =
      headers.find(header_name);
  if (it != headers.end())
    return it->second;
  return std::string();
}

bool HttpServerRequestInfo::HasHeaderValue(
    const std::string& header_name,
    const std::string& header_value) const {
  DCHECK_EQ(base::ToLowerASCII(header_value), header_value);
  std::string complete_value = base::ToLowerASCII(GetHeaderValue(header_name));

  for (const base::StringPiece& cur :
       base::SplitString(complete_value, ",", base::KEEP_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    if (base::TrimString(cur, " \t", base::TRIM_ALL) == header_value)
      return true;
  }
  return false;
}

}  // namespace net
