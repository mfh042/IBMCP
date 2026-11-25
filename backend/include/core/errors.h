#pragma once

#include <exception>
#include <string>

#include <json/json.h>

namespace ibmcp {

class BizError : public std::exception {
  public:
    BizError(int code, std::string message)
        : code_(code), message_(std::move(message)) {}

    BizError(int code, std::string message, Json::Value data)
        : code_(code), message_(std::move(message)), data_(std::move(data)) {}

    int code() const { return code_; }
    const std::string &message() const { return message_; }
    const Json::Value &data() const { return data_; }
    const char *what() const noexcept override { return message_.c_str(); }

  private:
    int code_;
    std::string message_;
    Json::Value data_;
};

namespace err {
constexpr int OK = 0;
constexpr int BAD_REQUEST = 400;
constexpr int UNAUTHORIZED = 401;
constexpr int FORBIDDEN = 403;
constexpr int NOT_FOUND = 404;
constexpr int CONFLICT = 409;
constexpr int VALIDATION_FAILED = 422;
constexpr int INTERNAL = 500;
constexpr int COLLECTOR_ERROR = 550;
}  // namespace err

}  // namespace ibmcp