#include "opcua_driver.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef IBMCP_HAS_OPCUA
#if __has_include(<open62541.h>)
#include <open62541.h>
#elif __has_include(<open62541/open62541.h>)
#include <open62541/open62541.h>
#endif
#endif

namespace ibmcp {

OpcUaDriver::OpcUaDriver() = default;
OpcUaDriver::~OpcUaDriver() { disconnect(); }

#ifdef IBMCP_HAS_OPCUA

namespace {
// 从 UA_Variant 解码出一个 double（数值类）或 bool/string 标记。
// 返回是否成功解码为数值；out 填充数值，is_bool/out_bool / is_str/out_str 处理特殊类型。
bool variant_to_scalar(const UA_Variant& v, double& out, bool& is_bool, bool& out_bool,
                       bool& is_str, std::string& out_str) {
    if (v.type == nullptr || !UA_Variant_isScalar(&v) || v.data == nullptr) return false;
    const UA_DataType* t = v.type;
    if (t == &UA_TYPES[UA_TYPES_DOUBLE]) {
        out = *static_cast<const UA_Double*>(v.data);
    } else if (t == &UA_TYPES[UA_TYPES_FLOAT]) {
        out = static_cast<double>(*static_cast<const UA_Float*>(v.data));
    } else if (t == &UA_TYPES[UA_TYPES_INT16]) {
        out = static_cast<double>(*static_cast<const UA_Int16*>(v.data));
    } else if (t == &UA_TYPES[UA_TYPES_INT32]) {
        out = static_cast<double>(*static_cast<const UA_Int32*>(v.data));
    } else if (t == &UA_TYPES[UA_TYPES_INT64]) {
        out = static_cast<double>(*static_cast<const UA_Int64*>(v.data));
    } else if (t == &UA_TYPES[UA_TYPES_UINT16]) {
        out = static_cast<double>(*static_cast<const UA_UInt16*>(v.data));
    } else if (t == &UA_TYPES[UA_TYPES_UINT32]) {
        out = static_cast<double>(*static_cast<const UA_UInt32*>(v.data));
    } else if (t == &UA_TYPES[UA_TYPES_UINT64]) {
        out = static_cast<double>(*static_cast<const UA_UInt64*>(v.data));
    } else if (t == &UA_TYPES[UA_TYPES_BYTE]) {
        out = static_cast<double>(*static_cast<const UA_Byte*>(v.data));
    } else if (t == &UA_TYPES[UA_TYPES_SBYTE]) {
        out = static_cast<double>(*static_cast<const UA_SByte*>(v.data));
    } else if (t == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        is_bool = true;
        out_bool = (*static_cast<const UA_Boolean*>(v.data)) != 0;
    } else if (t == &UA_TYPES[UA_TYPES_STRING]) {
        const UA_String* s = static_cast<const UA_String*>(v.data);
        is_str = true;
        out_str.assign(reinterpret_cast<const char*>(s->data), s->length);
    } else {
        return false;
    }
    return true;
}
} // namespace

bool OpcUaDriver::connect(const nlohmann::json& connection,
                          int connect_timeout_ms, int read_timeout_ms) {
    disconnect();
    if (!connection.contains("endpoint") || !connection["endpoint"].is_string()) return false;
    std::string endpoint = connection["endpoint"].get<std::string>();

    UA_Client* client = UA_Client_new();
    if (!client) return false;
    client_ = client;
    UA_ClientConfig* config = UA_Client_getConfig(client);
    UA_ClientConfig_setDefault(config);
    if (connect_timeout_ms > 0) {
        config->timeout = static_cast<UA_UInt32>(connect_timeout_ms);
    }

    if (connection.contains("username") && connection["username"].is_string()) {
        std::string u = connection["username"].get<std::string>();
        std::string p;
        if (connection.contains("password") && connection["password"].is_string()) {
            p = connection["password"].get<std::string>();
        }
        UA_UserNameIdentityToken* ukit = UA_UserNameIdentityToken_new();
        ukit->userName = UA_STRING_ALLOC(u.c_str());
        ukit->password = UA_STRING_ALLOC(p.c_str());
        config->userIdentityToken.encoding = UA_EXTENSIONOBJECT_DECODED;
        config->userIdentityToken.content.decoded.type =
            &UA_TYPES[UA_TYPES_USERNAMEIDENTITYTOKEN];
        config->userIdentityToken.content.decoded.data = ukit;
    }

    UA_StatusCode rc = UA_Client_connect(client, endpoint.c_str());
    if (rc != UA_STATUSCODE_GOOD) {
        UA_Client_delete(client);
        client_ = nullptr;
        return false;
    }
    connected_ = true;
    (void)read_timeout_ms;
    return true;
}

std::vector<ReadResult> OpcUaDriver::read_batch(const std::vector<Point>& points) {
    std::vector<ReadResult> out;
    out.reserve(points.size());
    if (!connected_ || !client_) {
        for (const auto& pt : points) {
            out.push_back({pt.point_id, "", "Bad", "not connected"});
        }
        return out;
    }
    UA_Client* client = static_cast<UA_Client*>(client_);
    for (const auto& pt : points) {
        ReadResult r;
        r.point_id = pt.point_id;
        UA_NodeId nodeId;
        UA_NodeId_init(&nodeId);
        UA_String addrStr = UA_STRING_ALLOC(pt.address.c_str());
        UA_StatusCode parseRc = UA_NodeId_parse(&nodeId, addrStr);
        UA_String_clear(&addrStr);
        if (parseRc != UA_STATUSCODE_GOOD) {
            UA_NodeId_clear(&nodeId);
            r.quality = "Bad";
            r.error = "invalid node id";
            out.push_back(std::move(r));
            continue;
        }
        UA_Variant variant;
        UA_Variant_init(&variant);
        UA_StatusCode rc = UA_Client_readValueAttribute(client, nodeId, &variant);
        if (rc != UA_STATUSCODE_GOOD) {
            r.quality = "Bad";
            r.error = "read failed";
            out.push_back(std::move(r));
            UA_NodeId_clear(&nodeId);
            continue;
        }
        double dval = 0.0;
        bool is_bool = false, out_bool = false;
        bool is_str = false;
        std::string out_str;
        bool ok = variant_to_scalar(variant, dval, is_bool, out_bool, is_str, out_str);
        UA_Variant_clear(&variant);
        UA_NodeId_clear(&nodeId);
        if (!ok) {
            r.quality = "Bad";
            r.error = "unsupported type";
            out.push_back(std::move(r));
            continue;
        }
        if (is_str) {
            r.value = out_str;
        } else if (is_bool || pt.data_type == "bool") {
            r.value = (is_bool ? out_bool : (dval != 0.0)) ? "1" : "0";
        } else {
            double v = dval * pt.scale + pt.offset;
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%.3f", v);
            r.value = tmp;
        }
        r.quality = "Good";
        out.push_back(std::move(r));
    }
    return out;
}

bool OpcUaDriver::write(const Point& point, const std::string& value) {
    if (!connected_ || !client_) return false;
    UA_Client* client = static_cast<UA_Client*>(client_);
    UA_NodeId nodeId;
    UA_NodeId_init(&nodeId);
    UA_String addrStr = UA_STRING_ALLOC(point.address.c_str());
    UA_StatusCode parseRc = UA_NodeId_parse(&nodeId, addrStr);
    UA_String_clear(&addrStr);
    if (parseRc != UA_STATUSCODE_GOOD) {
        UA_NodeId_clear(&nodeId);
        return false;
    }

    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_StatusCode rc;
    if (point.data_type == "bool") {
        UA_Boolean b = (value != "0" && value != "false" && value != "False" && value != "FALSE");
        UA_Variant_setScalar(&variant, &b, &UA_TYPES[UA_TYPES_BOOLEAN]);
        rc = UA_Client_writeValueAttribute(client, nodeId, &variant);
    } else if (point.data_type == "string") {
        UA_String s = UA_STRING_ALLOC(value.c_str());
        UA_Variant_setScalar(&variant, &s, &UA_TYPES[UA_TYPES_STRING]);
        rc = UA_Client_writeValueAttribute(client, nodeId, &variant);
        UA_String_clear(&s);
    } else {
        try {
            UA_Double dval = std::stod(value);
            UA_Variant_setScalar(&variant, &dval, &UA_TYPES[UA_TYPES_DOUBLE]);
            rc = UA_Client_writeValueAttribute(client, nodeId, &variant);
        } catch (...) {
            UA_NodeId_clear(&nodeId);
            return false;
        }
    }
    UA_NodeId_clear(&nodeId);
    return rc == UA_STATUSCODE_GOOD;
}

void OpcUaDriver::disconnect() {
    if (client_) {
        UA_Client* client = static_cast<UA_Client*>(client_);
        UA_Client_disconnect(client);
        UA_Client_delete(client);
        client_ = nullptr;
    }
    connected_ = false;
}

#else

bool OpcUaDriver::connect(const nlohmann::json&, int, int) { return false; }
std::vector<ReadResult> OpcUaDriver::read_batch(const std::vector<Point>& points) {
    std::vector<ReadResult> out;
    for (const auto& pt : points) out.push_back({pt.point_id, "", "Bad", "opcua unavailable"});
    return out;
}
bool OpcUaDriver::write(const Point&, const std::string&) { return false; }
void OpcUaDriver::disconnect() {}

#endif

} // namespace ibmcp