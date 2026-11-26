#include "controller/node_red_controller.h"

#include "controller/common.h"
#include "core/errors.h"
#include "core/result.h"
#include "util/shell_util.h"

using namespace drogon;

namespace ibmcp {

namespace {

constexpr const char *kContainer = "ibmcp-nodered";
constexpr const char *kEditorHost = "127.0.0.1";
constexpr int kEditorPort = 1880;
constexpr int kStopTimeoutSec = 5;

// Absolute paths so the systemd service (minimal PATH) can find them.
constexpr const char *kSudo = "/usr/bin/sudo";
constexpr const char *kDocker = "/usr/bin/docker";

Json::Value collectStatus() {
    Json::Value data(Json::objectValue);
    auto r = util::shellExec(std::string(kSudo) + " -n " + kDocker +
                             " inspect -f '{{.State.Status}}' " + kContainer);
    std::string state;
    if (r.ok() && !r.output.empty()) {
        state = r.output;
    } else {
        state = "missing";
    }
    data["container"] = state;
    data["editorReachable"] = util::tcpReachable(kEditorHost, kEditorPort, 1200);
    data["updatedAt"] = static_cast<Json::Int64>(ctrl::nowEpoch());
    return data;
}

}  // namespace

Task<HttpResponsePtr> NodeRedController::status(HttpRequestPtr req) {
    auth::requireAuth(req);
    co_return result::ok(collectStatus());
}

Task<HttpResponsePtr> NodeRedController::start(HttpRequestPtr req) {
    ctrl::requireAnyRole(req, {"admin"});
    auto r = util::shellExec(std::string(kSudo) + " -n " + kDocker + " start " + kContainer);
    if (!r.ok()) {
        co_return result::error(err::INTERNAL, std::string("docker start failed: ") + r.output);
    }
    co_return result::ok(collectStatus());
}

Task<HttpResponsePtr> NodeRedController::stop(HttpRequestPtr req) {
    ctrl::requireAnyRole(req, {"admin"});
    auto r = util::shellExec(std::string(kSudo) + " -n " + kDocker + " stop -t " +
                             std::to_string(kStopTimeoutSec) + " " + kContainer);
    if (!r.ok()) {
        co_return result::error(err::INTERNAL, std::string("docker stop failed: ") + r.output);
    }
    co_return result::ok(collectStatus());
}

Task<HttpResponsePtr> NodeRedController::restart(HttpRequestPtr req) {
    ctrl::requireAnyRole(req, {"admin"});
    auto r = util::shellExec(std::string(kSudo) + " -n " + kDocker + " restart -t " +
                             std::to_string(kStopTimeoutSec) + " " + kContainer);
    if (!r.ok()) {
        co_return result::error(err::INTERNAL, std::string("docker restart failed: ") + r.output);
    }
    co_return result::ok(collectStatus());
}

}  // namespace ibmcp
