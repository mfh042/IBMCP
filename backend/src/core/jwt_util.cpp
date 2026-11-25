#include "core/jwt_util.h"

#include "core/config.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <chrono>
#include <cstring>
#include <random>
#include <vector>

namespace ibmcp::jwt {

namespace {

std::string base64UrlEncode(const unsigned char *data, size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back(tbl[(n >> 6) & 0x3F]);
        out.push_back(tbl[n & 0x3F]);
        i += 3;
    }
    if (i < len) {
        uint32_t n = data[i] << 16;
        if (i + 1 < len) n |= data[i + 1] << 8;
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        if (i + 1 < len) out.push_back(tbl[(n >> 6) & 0x3F]);
    }
    return out;
}

std::vector<unsigned char> base64UrlDecode(const std::string &s) {
    auto b64 = s;
    for (auto &c : b64) {
        if (c == '-') c = '+';
        if (c == '_') c = '/';
    }
    while (b64.size() % 4) b64.push_back('=');
    std::vector<unsigned char> out((b64.size() / 4) * 3);
    size_t len = 0;
    for (size_t i = 0, j = 0; i < b64.size(); i += 4) {
        auto val = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return 0;
        };
        uint32_t n = (val(b64[i]) << 18) | (val(b64[i + 1]) << 12) |
                     (val(b64[i + 2]) << 6) | val(b64[i + 3]);
        out[j++] = (n >> 16) & 0xFF;
        out[j++] = (n >> 8) & 0xFF;
        out[j++] = n & 0xFF;
        len = j;
    }
    if (b64.size() >= 4) {
        if (b64[b64.size() - 1] == '=') len--;
        if (b64[b64.size() - 2] == '=') len--;
    }
    out.resize(len);
    return out;
}

std::string hmacSha256(const std::string &key, const std::string &msg) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char *>(msg.data()), msg.size(), md, &mdLen);
    return std::string(reinterpret_cast<char *>(md), mdLen);
}

Json::Value parseJson(const std::string &s) {
    Json::Value v;
    Json::CharReaderBuilder b;
    std::string errs;
    std::unique_ptr<Json::CharReader> r(b.newCharReader());
    r->parse(s.data(), s.data() + s.size(), &v, &errs);
    return v;
}

// 128-bit 随机 hex（32 字符），每次签发唯一，用作 jti。
std::string genJti() {
    static thread_local std::mt19937_64 gen(std::random_device{}());
    static thread_local std::uniform_int_distribution<uint64_t> dist;
    unsigned char bytes[16];
    uint64_t a = dist(gen);
    uint64_t b = dist(gen);
    std::memcpy(bytes, &a, 8);
    std::memcpy(bytes + 8, &b, 8);
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 16; ++i) {
        out.push_back(hex[(bytes[i] >> 4) & 0xF]);
        out.push_back(hex[bytes[i] & 0xF]);
    }
    return out;
}

}  // namespace

std::string createToken(const std::string &sub, const Json::Value &extra) {
    Json::Value header;
    header["alg"] = "HS256";
    header["typ"] = "JWT";

    Json::Value payload = extra;
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    payload["sub"] = sub;
    payload["iat"] = static_cast<Json::Int64>(now);
    payload["exp"] = static_cast<Json::Int64>(
        now + static_cast<int64_t>(config::accessTokenExpireMinutes()) * 60);
    payload["jti"] = genJti();

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    std::string h = Json::writeString(wb, header);
    std::string p = Json::writeString(wb, payload);

    std::string signingInput =
        base64UrlEncode(reinterpret_cast<const unsigned char *>(h.data()), h.size()) + "." +
        base64UrlEncode(reinterpret_cast<const unsigned char *>(p.data()), p.size());

    std::string sig = hmacSha256(config::appSecretKey(), signingInput);
    return signingInput + "." +
           base64UrlEncode(reinterpret_cast<const unsigned char *>(sig.data()), sig.size());
}

std::optional<Json::Value> verifyToken(const std::string &token) {
    auto dot1 = token.find('.');
    if (dot1 == std::string::npos) return std::nullopt;
    auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return std::nullopt;

    std::string hB64 = token.substr(0, dot1);
    std::string pB64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string sB64 = token.substr(dot2 + 1);

    auto expected = hmacSha256(config::appSecretKey(), hB64 + "." + pB64);
    auto got = base64UrlDecode(sB64);
    if (got.size() != expected.size() ||
        CRYPTO_memcmp(got.data(), expected.data(), expected.size()) != 0) {
        return std::nullopt;
    }

    auto headerBytes = base64UrlDecode(hB64);
    auto header = parseJson(std::string(headerBytes.begin(), headerBytes.end()));
    if (header.get("alg", "").asString() != "HS256") return std::nullopt;

    auto payloadBytes = base64UrlDecode(pB64);
    auto payload = parseJson(std::string(payloadBytes.begin(), payloadBytes.end()));

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    if (payload.isMember("exp") &&
        payload["exp"].asInt64() < static_cast<Json::Int64>(now)) {
        return std::nullopt;
    }
    return payload;
}

}  // namespace ibmcp::jwt