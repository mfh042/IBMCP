#include "db/db.h"

#include "core/config.h"

namespace ibmcp::db {

drogon::orm::DbClientPtr ro() {
    return drogon::app().getDbClient(config::kDefaultDb);
}

drogon::orm::DbClientPtr rw() {
    return drogon::app().getDbClient(config::kWriteDb);
}

}  // namespace ibmcp::db