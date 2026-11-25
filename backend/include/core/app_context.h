#pragma once

#include <memory>

#include "collector/collector_bridge.h"

namespace ibmcp {

class EventDispatcher;
class SamplePump;
class CollectorOrchestrator;

class AppContext {
  public:
    static AppContext &instance();

    void init();
    void shutdown();

    CollectorBridge &collector() { return bridge_; }
    CollectorOrchestrator &orchestrator();

  private:
    CollectorBridge bridge_;
    std::unique_ptr<EventDispatcher> dispatcher_;
    std::unique_ptr<SamplePump> pump_;
    std::unique_ptr<CollectorOrchestrator> orchestrator_;
};

}  // namespace ibmcp