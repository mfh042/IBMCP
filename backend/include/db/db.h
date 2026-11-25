#pragma once

#include <drogon/orm/DbClient.h>

#include <string>

namespace ibmcp::db {

drogon::orm::DbClientPtr ro();
drogon::orm::DbClientPtr rw();

}  // namespace ibmcp::db