#pragma once

#include <drogon/HttpController.h>

namespace ibmcp {

class ConfigController : public drogon::HttpController<ConfigController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ConfigController::listEquipments, "/api/v1/config/equipments", drogon::Get);
    ADD_METHOD_TO(ConfigController::recompileAll, "/api/v1/config/equipments/recompile",
                  drogon::Post);
    ADD_METHOD_TO(ConfigController::equipmentByCode, "/api/v1/config/equipments/code/{1}",
                  drogon::Get);
    ADD_METHOD_TO(ConfigController::equipmentStatus, "/api/v1/config/equipments/{1}/status",
                  drogon::Get);
    ADD_METHOD_TO(ConfigController::equipmentExpand, "/api/v1/config/equipments/{1}/expand",
                  drogon::Get);
    ADD_METHOD_TO(ConfigController::equipmentPlan, "/api/v1/config/equipments/{1}/plan",
                  drogon::Get);
    ADD_METHOD_TO(ConfigController::activateEquipment, "/api/v1/config/equipments/{1}/activate",
                  drogon::Post);
    ADD_METHOD_TO(ConfigController::unloadEquipment, "/api/v1/config/equipments/{1}/unload",
                  drogon::Post);
    ADD_METHOD_TO(ConfigController::syncEquipment, "/api/v1/config/equipments/{1}/sync",
                  drogon::Post);
    ADD_METHOD_TO(ConfigController::cleanupEquipment, "/api/v1/config/equipments/{1}/cleanup",
                  drogon::Delete);
    ADD_METHOD_TO(ConfigController::getEquipment, "/api/v1/config/equipments/{1}", drogon::Get);
    ADD_METHOD_TO(ConfigController::createEquipment, "/api/v1/config/equipments", drogon::Post);
    ADD_METHOD_TO(ConfigController::updateEquipment, "/api/v1/config/equipments/{1}", drogon::Put);
    ADD_METHOD_TO(ConfigController::deleteEquipment, "/api/v1/config/equipments/{1}",
                  drogon::Delete);
    ADD_METHOD_TO(ConfigController::listRevisions, "/api/v1/config/revisions", drogon::Get);
    METHOD_LIST_END

    drogon::Task<drogon::HttpResponsePtr> listEquipments(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> equipmentByCode(drogon::HttpRequestPtr req,
                                                          std::string code);
    drogon::Task<drogon::HttpResponsePtr> getEquipment(drogon::HttpRequestPtr req,
                                                       int64_t configId);
    drogon::Task<drogon::HttpResponsePtr> equipmentStatus(drogon::HttpRequestPtr req,
                                                          int64_t configId);
    drogon::Task<drogon::HttpResponsePtr> equipmentExpand(drogon::HttpRequestPtr req,
                                                          int64_t configId);
    drogon::Task<drogon::HttpResponsePtr> equipmentPlan(drogon::HttpRequestPtr req,
                                                        int64_t configId);
    drogon::Task<drogon::HttpResponsePtr> listRevisions(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> createEquipment(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> updateEquipment(drogon::HttpRequestPtr req,
                                                          int64_t configId);
    drogon::Task<drogon::HttpResponsePtr> activateEquipment(drogon::HttpRequestPtr req,
                                                            int64_t configId);
    drogon::Task<drogon::HttpResponsePtr> unloadEquipment(drogon::HttpRequestPtr req,
                                                          int64_t configId);
    drogon::Task<drogon::HttpResponsePtr> recompileAll(drogon::HttpRequestPtr req);
    drogon::Task<drogon::HttpResponsePtr> deleteEquipment(drogon::HttpRequestPtr req,
                                                          int64_t configId);
    drogon::Task<drogon::HttpResponsePtr> syncEquipment(drogon::HttpRequestPtr req,
                                                        int64_t configId);
    drogon::Task<drogon::HttpResponsePtr> cleanupEquipment(drogon::HttpRequestPtr req,
                                                           int64_t configId);
};

}  // namespace ibmcp