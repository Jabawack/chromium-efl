// Copyright 2014 Samsung Electronics. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POLICY_RESPONSE_DELEGATE_EFL_H_
#define POLICY_RESPONSE_DELEGATE_EFL_H_

#include "base/memory/ref_counted.h"
#include "net/base/completion_callback.h"
#include "url/gurl.h"

namespace net {
class URLRequest;
class HttpResponseHeaders;
}

namespace tizen_webview {
class PolicyDecision;
}

class PolicyResponseDelegateEfl: public base::RefCountedThreadSafe<PolicyResponseDelegateEfl> {
 public:
  PolicyResponseDelegateEfl(net::URLRequest* request,
                            const net::CompletionCallback& callback,
                            const net::HttpResponseHeaders* original_response_headers);
  virtual void UseResponse();
  virtual void IgnoreResponse();
  void HandleURLRequestDestroyedOnIOThread();

  int GetRenderProcessId() const { return render_process_id_; }
  int GetRenderFrameId() const { return render_frame_id_; }
  int GetRenderViewId() const { return render_view_id_; }

 private:
  friend class base::RefCountedThreadSafe<PolicyResponseDelegateEfl>;

  void HandlePolicyResponseOnUIThread();
  void UseResponseOnIOThread();
  void IgnoreResponseOnIOThread();

  scoped_ptr<tizen_webview::PolicyDecision> policy_decision_;
  net::CompletionCallback callback_;
  int render_process_id_;
  int render_frame_id_;
  int render_view_id_;
  // Should be accessed only on IO thread.
  bool processed_;
};

#endif /* POLICY_RESPONSE_DELEGATE_EFL_H_ */
