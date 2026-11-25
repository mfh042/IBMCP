#pragma once

#include <string>

namespace ibmcp::db {

class Migrator {
  public:
    Migrator(std::string migrationsDir, std::string dsn);
    void migrate();

  private:
    std::string dir_;
    std::string dsn_;
};

}  // namespace ibmcp::db