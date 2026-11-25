#include "core/password_util.h"

#include "core/errors.h"

#include <openssl/rand.h>

#include <cstring>

#ifdef IBMCP_HAS_ARGON2
#include <argon2.h>
#endif

namespace ibmcp::password {

#ifdef IBMCP_HAS_ARGON2

bool verify(const std::string &raw, const std::string &hashed) {
    if (hashed.empty()) return false;
    return argon2id_verify(hashed.c_str(), raw.data(), raw.size()) == ARGON2_OK;
}

std::string hash(const std::string &raw) {
    unsigned char salt[16];
    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        throw BizError(err::INTERNAL, "failed to generate salt");
    }
    char encoded[512];
    int rc = argon2id_hash_encoded(3, 65536, 4, raw.data(), raw.size(), salt,
                                   sizeof(salt), 32, encoded, sizeof(encoded));
    if (rc != ARGON2_OK) {
        throw BizError(err::INTERNAL, std::string("argon2 hash failed: ") + argon2_error_message(rc));
    }
    return std::string(encoded);
}

#else

bool verify(const std::string &, const std::string &) { return false; }

std::string hash(const std::string &) {
    throw BizError(err::INTERNAL, "argon2 not available");
}

#endif

}  // namespace ibmcp::password