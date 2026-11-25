#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ibmcp::service {

struct ResourceSpec {
    std::string table;
    std::string code;
    std::string name;
};

class StaticDictService {
  public:
    static StaticDictService &instance();

    drogon::Task<Json::Value> bootstrap();
    drogon::Task<Json::Value> page(const std::string &resource, int page, int size,
                                    const std::string &keyword,
                                    const std::map<std::string, std::string> &filters);
    drogon::Task<Json::Value> all(const std::string &resource);
    drogon::Task<Json::Value> byCode(const std::string &resource, const std::string &code);
    drogon::Task<Json::Value> byId(const std::string &resource, int64_t rowId);
    drogon::Task<Json::Value> create(const std::string &resource, const Json::Value &body);
    drogon::Task<Json::Value> update(const std::string &resource, int64_t rowId,
                                     const Json::Value &body);
    drogon::Task<Json::Value> remove(const std::string &resource, int64_t rowId);

  private:
    struct ColMeta {
        std::string dataType;
        bool nullable = true;
        bool hasMaxLen = false;
        long maxLen = 0;
        bool hasDefault = false;
    };

    static const std::map<std::string, ResourceSpec> &specs();
    static const std::map<std::string, std::vector<std::pair<std::string, std::string>>>
        &referenceSpecs();

    const ResourceSpec &spec(const std::string &resource) const;
    drogon::Task<std::map<std::string, ColMeta>> columns(const std::string &table);
    std::string coerce(const std::string &resource, const Json::Value &value,
                       const std::string &col, const ColMeta &meta) const;
    std::map<std::string, std::string> validateBody(const std::string &resource,
                                                     const Json::Value &body,
                                                     const std::map<std::string, ColMeta> &meta,
                                                     bool partial) const;
    drogon::Task<void> crossCheck(const std::string &resource,
                                  const std::map<std::string, std::string> &data) const;
    drogon::Task<void> checkReferences(const std::string &resource, int64_t rowId) const;

    std::mutex mu_;
    std::map<std::string, std::map<std::string, ColMeta>> cache_;
};

}  // namespace ibmcp::service