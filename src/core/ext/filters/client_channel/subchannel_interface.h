//
// Copyright 2019 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INTERFACE_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INTERFACE_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/connectivity_state.h>
#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/iomgr/pollset_set.h"

namespace grpc_core {

// The interface for subchannels that is exposed to LB policy implementations.
class SubchannelInterface : public RefCounted<SubchannelInterface> {
 public:
  class ConnectivityStateWatcherInterface {
   public:
    virtual ~ConnectivityStateWatcherInterface() = default;

    // Will be invoked whenever the subchannel's connectivity state
    // changes.  There will be only one invocation of this method on a
    // given watcher instance at any given time.
    virtual void OnConnectivityStateChange(
        grpc_connectivity_state new_state) = 0;

    // TODO(roth): Remove this as soon as we move to EventManager-based
    // polling.
    virtual grpc_pollset_set* interested_parties() = 0;
  };

  // Opaque interface for watching data of a particular type for this
  // subchannel.
  class DataWatcherInterface {
   public:
    virtual ~DataWatcherInterface() = default;
  };

  explicit SubchannelInterface(const char* trace = nullptr)
      : RefCounted<SubchannelInterface>(trace) {}

  ~SubchannelInterface() override = default;

  // Returns the current connectivity state of the subchannel.
  virtual grpc_connectivity_state CheckConnectivityState() = 0;

  // Starts watching the subchannel's connectivity state.
  // The first callback to the watcher will be delivered when the
  // subchannel's connectivity state becomes a value other than
  // initial_state, which may happen immediately.
  // Subsequent callbacks will be delivered as the subchannel's state
  // changes.
  // The watcher will be destroyed either when the subchannel is
  // destroyed or when CancelConnectivityStateWatch() is called.
  // There can be only one watcher of a given subchannel.  It is not
  // valid to call this method a second time without first cancelling
  // the previous watcher using CancelConnectivityStateWatch().
  virtual void WatchConnectivityState(
      grpc_connectivity_state initial_state,
      std::unique_ptr<ConnectivityStateWatcherInterface> watcher) = 0;

  // Cancels a connectivity state watch.
  // If the watcher has already been destroyed, this is a no-op.
  virtual void CancelConnectivityStateWatch(
      ConnectivityStateWatcherInterface* watcher) = 0;

  // Attempt to connect to the backend.  Has no effect if already connected.
  // If the subchannel is currently in backoff delay due to a previously
  // failed attempt, the new connection attempt will not start until the
  // backoff delay has elapsed.
  virtual void RequestConnection() = 0;

  // Resets the subchannel's connection backoff state.  If RequestConnection()
  // has been called since the subchannel entered TRANSIENT_FAILURE state,
  // starts a new connection attempt immediately; otherwise, a new connection
  // attempt will be started as soon as RequestConnection() is called.
  virtual void ResetBackoff() = 0;

  // Registers a new data watcher.
  virtual void AddDataWatcher(
      std::unique_ptr<DataWatcherInterface> watcher) = 0;

  // TODO(roth): Need a better non-grpc-specific abstraction here.
  virtual const grpc_channel_args* channel_args() = 0;
};

// A class that delegates to another subchannel, to be used in cases
// where an LB policy needs to wrap a subchannel.
class DelegatingSubchannel : public SubchannelInterface {
 public:
  explicit DelegatingSubchannel(RefCountedPtr<SubchannelInterface> subchannel)
      : wrapped_subchannel_(std::move(subchannel)) {}

  RefCountedPtr<SubchannelInterface> wrapped_subchannel() const {
    return wrapped_subchannel_;
  }

  grpc_connectivity_state CheckConnectivityState() override {
    return wrapped_subchannel_->CheckConnectivityState();
  }
  void WatchConnectivityState(
      grpc_connectivity_state initial_state,
      std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override {
    return wrapped_subchannel_->WatchConnectivityState(initial_state,
                                                       std::move(watcher));
  }
  void CancelConnectivityStateWatch(
      ConnectivityStateWatcherInterface* watcher) override {
    return wrapped_subchannel_->CancelConnectivityStateWatch(watcher);
  }
  void RequestConnection() override {
    wrapped_subchannel_->RequestConnection();
  }
  void ResetBackoff() override { wrapped_subchannel_->ResetBackoff(); }
  const grpc_channel_args* channel_args() override {
    return wrapped_subchannel_->channel_args();
  }
  void AddDataWatcher(std::unique_ptr<DataWatcherInterface> watcher) override {
    wrapped_subchannel_->AddDataWatcher(std::move(watcher));
  }

 private:
  RefCountedPtr<SubchannelInterface> wrapped_subchannel_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_INTERFACE_H
