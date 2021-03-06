// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_IN_MEMORY_CACHE_H_
#define NET_TOOLS_QUIC_QUIC_IN_MEMORY_CACHE_H_

#include <map>
#include <string>

#include "base/containers/hash_tables.h"
#include "base/memory/singleton.h"
#include "base/strings/string_piece.h"
#include "net/quic/spdy_utils.h"
#include "net/spdy/spdy_framer.h"
#include "url/gurl.h"

using base::StringPiece;
using std::string;
using std::list;

namespace base {

template <typename Type> struct DefaultSingletonTraits;

}  // namespace base

namespace net {
namespace tools {

namespace test {
class QuicInMemoryCachePeer;
}  // namespace test

class QuicServer;

// In-memory cache for HTTP responses.
// Reads from disk cache generated by:
// `wget -p --save_headers <url>`
class QuicInMemoryCache {
 public:
  // A ServerPushInfo contains path of the push request and everything needed in
  // comprising a response for the push request.
  struct ServerPushInfo {
    ServerPushInfo(GURL request_url,
                   const net::SpdyHeaderBlock& headers,
                   net::SpdyPriority priority,
                   string body);
    GURL request_url;
    net::SpdyHeaderBlock headers;
    net::SpdyPriority priority;
    string body;
  };

  enum SpecialResponseType {
    REGULAR_RESPONSE,  // Send the headers and body like a server should.
    CLOSE_CONNECTION,  // Close the connection (sending the close packet).
    IGNORE_REQUEST,  // Do nothing, expect the client to time out.
  };

  // Container for response header/body pairs.
  class Response {
   public:
    Response();
    ~Response();

    SpecialResponseType response_type() const { return response_type_; }
    const SpdyHeaderBlock& headers() const { return headers_; }
    const SpdyHeaderBlock& trailers() const { return trailers_; }
    const base::StringPiece body() const { return base::StringPiece(body_); }

    void set_response_type(SpecialResponseType response_type) {
      response_type_ = response_type;
    }
    void set_headers(const SpdyHeaderBlock& headers) {
      headers_ = headers;
    }
    void set_trailers(const SpdyHeaderBlock& trailers) { trailers_ = trailers; }
    void set_body(base::StringPiece body) {
      body.CopyToString(&body_);
    }

   private:
    SpecialResponseType response_type_;
    SpdyHeaderBlock headers_;
    SpdyHeaderBlock trailers_;
    std::string body_;

    DISALLOW_COPY_AND_ASSIGN(Response);
  };

  // Returns the singleton instance of the cache.
  static QuicInMemoryCache* GetInstance();

  // Retrieve a response from this cache for a given host and path..
  // If no appropriate response exists, nullptr is returned.
  const Response* GetResponse(base::StringPiece host,
                              base::StringPiece path) const;

  // Adds a simple response to the cache.  The response headers will
  // only contain the "content-length" header with the length of |body|.
  void AddSimpleResponse(base::StringPiece host,
                         base::StringPiece path,
                         int response_code,
                         base::StringPiece body);

  // Add a simple response to the cache as AddSimpleResponse() does, and add
  // some server push resources(resource path, corresponding response status and
  // path) associated with it.
  // Push resource implicitly come from the same host.
  void AddSimpleResponseWithServerPushResources(
      StringPiece host,
      StringPiece path,
      int response_code,
      StringPiece body,
      list<ServerPushInfo> push_resources);

  // Add a response to the cache.
  void AddResponse(base::StringPiece host,
                   base::StringPiece path,
                   const SpdyHeaderBlock& response_headers,
                   base::StringPiece response_body);

  // Add a response, with trailers, to the cache.
  void AddResponse(base::StringPiece host,
                   base::StringPiece path,
                   const SpdyHeaderBlock& response_headers,
                   base::StringPiece response_body,
                   const SpdyHeaderBlock& response_trailers);

  // Simulate a special behavior at a particular path.
  void AddSpecialResponse(base::StringPiece host,
                          base::StringPiece path,
                          SpecialResponseType response_type);

  // Sets a default response in case of cache misses.  Takes ownership of
  // 'response'.
  void AddDefaultResponse(Response* response);

  // |cache_cirectory| can be generated using `wget -p --save-headers <url>`.
  void InitializeFromDirectory(const string& cache_directory);

  // Find all the server push resources associated with |request_url|.
  list<ServerPushInfo> GetServerPushResources(string request_url);

 private:
  typedef base::hash_map<string, Response*> ResponseMap;

  friend struct base::DefaultSingletonTraits<QuicInMemoryCache>;
  friend class test::QuicInMemoryCachePeer;

  QuicInMemoryCache();
  ~QuicInMemoryCache();

  void ResetForTests();

  void AddResponseImpl(base::StringPiece host,
                       base::StringPiece path,
                       SpecialResponseType response_type,
                       const SpdyHeaderBlock& response_headers,
                       base::StringPiece response_body,
                       const SpdyHeaderBlock& response_trailers);

  string GetKey(base::StringPiece host, base::StringPiece path) const;

  // Add some server push urls with given responses for specified
  // request if these push resources are not associated with this request yet.
  void MaybeAddServerPushResources(StringPiece request_host,
                                   StringPiece request_path,
                                   list<ServerPushInfo> push_resources);

  // Check if push resource(push_host/push_path) associated with given request
  // url already exists in server push map.
  bool PushResourceExistsInCache(string original_request_url,
                                 ServerPushInfo resource);

  // Cached responses.
  ResponseMap responses_;

  // The default response for cache misses, if set.
  scoped_ptr<Response> default_response_;

  // A map from request URL to associated server push responses (if any).
  std::multimap<string, ServerPushInfo> server_push_resources_;

  DISALLOW_COPY_AND_ASSIGN(QuicInMemoryCache);
};

}  // namespace tools
}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_IN_MEMORY_CACHE_H_
