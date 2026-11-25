#include "db/migrator.h"

#include "core/errors.h"

#include <libpq-fe.h>
#include <trantor/utils/Logger.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace ibmcp::db {

namespace fs = std::filesystem;

Migrator::Migrator(std::string migrationsDir, std::string dsn)
    : dir_(std::move(migrationsDir)), dsn_(std::move(dsn)) {}

static void exec(PGconn *conn, const std::string &sql) {
    PGresult *res = PQexec(conn, sql.c_str());
    ExecStatusType st = PQresultStatus(res);
    if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) {
        std::string err = PQerrorMessage(conn);
        PQclear(res);
        throw BizError(err::INTERNAL, "migration failed: " + err);
    }
    PQclear(res);
}

void Migrator::migrate() {
    PGconn *conn = PQconnectdb(dsn_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        std::string err = PQerrorMessage(conn);
        PQfinish(conn);
        throw BizError(err::INTERNAL, "migration connect failed: " + err);
    }

    exec(conn,
         "CREATE TABLE IF NOT EXISTS schema_migrations ("
         "version TEXT PRIMARY KEY, "
         "applied_at BIGINT NOT NULL DEFAULT EXTRACT(EPOCH FROM NOW())::BIGINT)");

    std::set<std::string> applied;
    {
        PGresult *res = PQexec(conn, "SELECT version FROM schema_migrations");
        if (PQresultStatus(res) == PGRES_TUPLES_OK) {
            int n = PQntuples(res);
            for (int i = 0; i < n; ++i) {
                applied.insert(PQgetvalue(res, i, 0));
            }
        }
        PQclear(res);
    }

    std::vector<std::string> files;
    if (fs::exists(dir_) && fs::is_directory(dir_)) {
        for (const auto &entry : fs::directory_iterator(dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".sql") {
                files.push_back(entry.path().filename().string());
            }
        }
    }
    std::sort(files.begin(), files.end());

    for (const auto &f : files) {
        if (applied.count(f)) continue;
        std::ifstream in((fs::path(dir_) / f).string());
        std::stringstream ss;
        ss << in.rdbuf();
        std::string sql = ss.str();
        exec(conn, sql);
        std::string mark = "INSERT INTO schema_migrations (version) VALUES ('" + f + "')";
        exec(conn, mark);
        LOG_INFO << "migration applied: " << f;
    }

    PQfinish(conn);
}

}  // namespace ibmcp::db