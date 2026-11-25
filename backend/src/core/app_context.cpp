#include "core/app_context.h"

#include "collector/event_dispatcher.h"
#include "collector/orchestrator.h"
#include "collector/sample_pump.h"
#include "control/command_waiter.h"
#include "core/config.h"
#include "db/db.h"
#include "ws/ws_hub.h"

namespace ibmcp {

AppContext &AppContext::instance() {
    static AppContext inst;
    return inst;
}

void AppContext::init() {
    bridge_.init(config::collectorCfg());
    dispatcher_ = std::make_unique<EventDispatcher>(
        db::rw(), bridge_, ws::WsHub::instance(), control::CommandWaiter::instance());
    dispatcher_->start();
    orchestrator_ = std::make_unique<CollectorOrchestrator>(db::rw(), bridge_);
    pump_ = std::make_unique<SamplePump>(
        db::rw(), config::pgDsn(), bridge_, ws::WsHub::instance(), config::samplePumpIntervalMs());
    pump_->start();
}

void AppContext::shutdown() {
    if (pump_) pump_->stop();
    if (dispatcher_) dispatcher_->stop();
    bridge_.shutdown();
}

CollectorOrchestrator &AppContext::orchestrator() {
    if (!orchestrator_) throw std::runtime_error("AppContext not initialized");
    return *orchestrator_;
}

}  // namespace ibmcp