// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_SPDY_UTILS_H_
#define NET_QUIC_SPDY_UTILS_H_

#include <map>
#include <string>

#include "net/base/net_export.h"
#include "net/quic/quic_protocol.h"
#include "net/spdy/spdy_framer.h"

namespace net {

class NET_EXPORT_PRIVATE SpdyUtils {
 public:
  static std::string SerializeUncompressedHeaders(
      const SpdyHeaderBlock& headers);

  // Parses |data| as a std::string containing serialized HTTP/2 HEADERS frame,
  // populating |headers| with the key->value std:pairs found.
  // |content_length| will be populated with the value of the content-length
  // header if one or more are present.
  // Returns true on success, false if parsing fails, or invalid keys are found.
  static bool ParseHeaders(const char* data,
                           uint32 data_len,
                           int* content_length,
                           SpdyHeaderBlock* headers);

  // Parses |data| as a std::string containing serialized HTTP/2 HEADERS frame,
  // populating |trailers| with the key->value std:pairs found.
  // The final offset header will be excluded from |trailers|, and instead the
  // value will be copied to |final_byte_offset|.
  // Returns true on success, false if parsing fails, or invalid keys are found.
  static bool ParseTrailers(const char* data,
                            uint32 data_len,
                            size_t* final_byte_offset,
                            SpdyHeaderBlock* trailers);

 private:
  DISALLOW_COPY_AND_ASSIGN(SpdyUtils);
};

}  // namespace net

#endif  // NET_QUIC_SPDY_UTILS_H_
