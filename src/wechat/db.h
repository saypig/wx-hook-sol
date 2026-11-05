// db.h -- read-only queries through the client's already-open SQLite handles
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace wxhook {
namespace db {

using Row = std::vector<std::pair<std::string, std::string>>;
using Rows = std::vector<Row>;

// Run `sql` against `dbName` (basename, e.g. "contact.db"). Returns false and
// sets *err when the handle is missing or the statement fails.
bool query(const std::string& dbName, const std::string& sql, Rows* out,
           std::string* err);

} // namespace db
} // namespace wxhook
