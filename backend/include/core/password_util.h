#pragma once

#include <string>

namespace ibmcp::password {

bool verify(const std::string &raw, const std::string &hashed);

std::string hash(const std::string &raw);

}  // namespace ibmcp::password