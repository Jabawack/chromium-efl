// Copyright 2013 The Chromium Authors. All rights reserved.
// Copyright 2014 Samsung Electronics. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _URL_REQUEST_CONTEXT_GETTER_EFL_H_
#define _URL_REQUEST_CONTEXT_GETTER_EFL_H_

#include "base/memory/scoped_ptr.h"
#include "content/public/browser/content_browser_client.h"
#include "net/url_request/url_request_context_getter.h"

namespace base {
class MessageLoop;
}

namespace net {
class HostResolver;
class NetworkDelegate;
class NetLog;
class ProxyConfigService;
class URLRequestContextStorage;
}

class EWebContext;

namespace content {

class URLRequestContextGetterEfl : public net::URLRequestContextGetter {
 public:
  URLRequestContextGetterEfl(
      EWebContext& web_context,
      bool ignore_certificate_errors,
      const base::FilePath& base_path,
      base::MessageLoop* io_loop,
      base::MessageLoop* file_loop,
      ProtocolHandlerMap* protocol_handlers,
      net::NetLog* net_log);

  // net::URLRequestContextGetter implementation.
  virtual net::URLRequestContext* GetURLRequestContext() OVERRIDE;
  virtual scoped_refptr<base::SingleThreadTaskRunner>
      GetNetworkTaskRunner() const OVERRIDE;

  net::HostResolver* host_resolver();

  void SetCookieStoragePath(const base::FilePath& path,
                            bool persist_session_cookies,
                            bool file_storage=true);

 protected:
  virtual ~URLRequestContextGetterEfl();

 private:
  void CreateSQLitePersistentCookieStore(
      const base::FilePath& path,
      bool persist_session_cookies);
  void CreatePersistentCookieStore(
      const base::FilePath& path,
      bool persist_session_cookies);

  EWebContext& web_context_;
  bool ignore_certificate_errors_;
  base::FilePath base_path_;
  base::MessageLoop* io_loop_;
  base::MessageLoop* file_loop_;
  net::NetLog* net_log_;

  base::FilePath cookie_store_path_;
  scoped_refptr<net::CookieStore> cookie_store_;

  scoped_ptr<net::ProxyConfigService> proxy_config_service_;
  scoped_ptr<net::NetworkDelegate> network_delegate_;
  scoped_ptr<net::URLRequestContextStorage> storage_;
  scoped_ptr<net::URLRequestContext> url_request_context_;
  ProtocolHandlerMap protocol_handlers_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestContextGetterEfl);
};

}; // namespace content

#endif  // _URL_REQUEST_CONTEXT_GETTER_EFL_H_