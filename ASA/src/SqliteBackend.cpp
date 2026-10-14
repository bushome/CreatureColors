// Local SQLite storage (UseMysql = false). The database file lives in the plugin folder.

#include "Database.h"

#include <filesystem>

#include <sqlite3.h>

namespace
{
    struct Db
    {
        sqlite3* handle = nullptr;
        std::string error;

        explicit Db(const std::string& path)
        {
            std::error_code ec;
            const std::filesystem::path p(path);
            if (p.has_parent_path())
                std::filesystem::create_directories(p.parent_path(), ec);
            if (ec)
            {
                error = "can't create folder " + p.parent_path().string() + ": " + ec.message();
                return;
            }

            if (sqlite3_open_v2(path.c_str(), &handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
            {
                error = handle ? sqlite3_errmsg(handle) : "sqlite3_open_v2 failed";
                sqlite3_close(handle);
                handle = nullptr;
                return;
            }

            // Wait instead of failing if another connection briefly holds the write lock.
            sqlite3_busy_timeout(handle, 5000);
        }

        ~Db()
        {
            if (handle)
                sqlite3_close(handle);
        }

        Db(const Db&) = delete;
        Db& operator=(const Db&) = delete;

        explicit operator bool() const { return handle != nullptr; }
    };

    struct Stmt
    {
        sqlite3_stmt* stmt = nullptr;
        int prepare_rc = SQLITE_ERROR;

        Stmt(sqlite3* db, const std::string& sql)
        {
            prepare_rc = sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
        }
        ~Stmt()
        {
            if (stmt)
                sqlite3_finalize(stmt);
        }
        Stmt(const Stmt&) = delete;
        Stmt& operator=(const Stmt&) = delete;

        bool ok() const { return prepare_rc == SQLITE_OK && stmt; }
    };

    void BindTemplate(sqlite3_stmt* stmt, const std::string& eos_id, const std::string& name, const ColorTemplate& tpl)
    {
        sqlite3_bind_text(stmt, 1, eos_id.c_str(), static_cast<int>(eos_id.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, name.c_str(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, tpl.species.c_str(), static_cast<int>(tpl.species.size()), SQLITE_TRANSIENT);
        for (int i = 0; i < 6; ++i)
            sqlite3_bind_int(stmt, 4 + i, tpl.regions[i]);
    }

    std::string InsertSql(const std::string& table)
    {
        return "INSERT INTO \"" + table + "\" (eos_id, template_name, species_class, "
               "region_0, region_1, region_2, region_3, region_4, region_5) "
               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    }

    class SqliteBackend final : public IBackend
    {
    public:
        explicit SqliteBackend(const DbSettings& s) : s_(s) {}

        bool EnsureTable(std::string& error) override
        {
            Db db(s_.sqlite_path);
            if (!db)
            {
                error = db.error;
                return false;
            }

            // template_name is case-insensitive, matching the MariaDB table's collation.
            const std::string sql =
                "CREATE TABLE IF NOT EXISTS \"" + s_.table + "\" ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "eos_id TEXT NOT NULL,"
                "template_name TEXT NOT NULL COLLATE NOCASE,"
                "species_class TEXT NOT NULL,"
                "region_0 INTEGER NOT NULL,"
                "region_1 INTEGER NOT NULL,"
                "region_2 INTEGER NOT NULL,"
                "region_3 INTEGER NOT NULL,"
                "region_4 INTEGER NOT NULL,"
                "region_5 INTEGER NOT NULL,"
                "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "UNIQUE (eos_id, template_name))";

            char* msg = nullptr;
            if (sqlite3_exec(db.handle, sql.c_str(), nullptr, nullptr, &msg) != SQLITE_OK)
            {
                error = msg ? msg : sqlite3_errmsg(db.handle);
                sqlite3_free(msg);
                return false;
            }
            return true;
        }

        InsertResult InsertIfAbsent(const std::string& eos_id, const std::string& name,
                                    const ColorTemplate& tpl, std::string& error) override
        {
            Db db(s_.sqlite_path);
            if (!db)
            {
                error = db.error;
                return InsertResult::Error;
            }

            Stmt st(db.handle, InsertSql(s_.table));
            if (!st.ok())
            {
                error = sqlite3_errmsg(db.handle);
                return InsertResult::Error;
            }

            BindTemplate(st.stmt, eos_id, name, tpl);
            const int rc = sqlite3_step(st.stmt);
            if (rc == SQLITE_DONE)
                return InsertResult::Inserted;
            if (sqlite3_extended_errcode(db.handle) == SQLITE_CONSTRAINT_UNIQUE)
                return InsertResult::AlreadyExists;

            error = sqlite3_errmsg(db.handle);
            return InsertResult::Error;
        }

        bool Upsert(const std::string& eos_id, const std::string& name, const ColorTemplate& tpl,
                    std::string& error) override
        {
            Db db(s_.sqlite_path);
            if (!db)
            {
                error = db.error;
                return false;
            }

            const std::string sql = InsertSql(s_.table) +
                " ON CONFLICT (eos_id, template_name) DO UPDATE SET"
                " species_class = excluded.species_class,"
                " region_0 = excluded.region_0, region_1 = excluded.region_1,"
                " region_2 = excluded.region_2, region_3 = excluded.region_3,"
                " region_4 = excluded.region_4, region_5 = excluded.region_5,"
                " updated_at = CURRENT_TIMESTAMP";

            Stmt st(db.handle, sql);
            if (!st.ok())
            {
                error = sqlite3_errmsg(db.handle);
                return false;
            }

            BindTemplate(st.stmt, eos_id, name, tpl);
            if (sqlite3_step(st.stmt) != SQLITE_DONE)
            {
                error = sqlite3_errmsg(db.handle);
                return false;
            }
            return true;
        }

        bool Fetch(const std::string& eos_id, const std::string& name,
                   std::optional<ColorTemplate>& out, std::string& error) override
        {
            out.reset();
            Db db(s_.sqlite_path);
            if (!db)
            {
                error = db.error;
                return false;
            }

            const std::string sql =
                "SELECT species_class, region_0, region_1, region_2, region_3, region_4, region_5 "
                "FROM \"" + s_.table + "\" WHERE eos_id = ? AND template_name = ? LIMIT 1";

            Stmt st(db.handle, sql);
            if (!st.ok())
            {
                error = sqlite3_errmsg(db.handle);
                return false;
            }

            sqlite3_bind_text(st.stmt, 1, eos_id.c_str(), static_cast<int>(eos_id.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(st.stmt, 2, name.c_str(), static_cast<int>(name.size()), SQLITE_TRANSIENT);

            const int rc = sqlite3_step(st.stmt);
            if (rc == SQLITE_DONE)
                return true; // no such template
            if (rc != SQLITE_ROW)
            {
                error = sqlite3_errmsg(db.handle);
                return false;
            }

            ColorTemplate tpl;
            const unsigned char* species = sqlite3_column_text(st.stmt, 0);
            tpl.species = species ? reinterpret_cast<const char*>(species) : "";
            for (int i = 0; i < 6; ++i)
                tpl.regions[i] = static_cast<uint8_t>(sqlite3_column_int(st.stmt, 1 + i));
            out = tpl;
            return true;
        }

        bool List(const std::string& eos_id, std::vector<TemplateEntry>& out, std::string& error) override
        {
            out.clear();
            Db db(s_.sqlite_path);
            if (!db)
            {
                error = db.error;
                return false;
            }

            Stmt st(db.handle, "SELECT template_name, species_class FROM \"" + s_.table +
                                   "\" WHERE eos_id = ? ORDER BY template_name COLLATE NOCASE");
            if (!st.ok())
            {
                error = sqlite3_errmsg(db.handle);
                return false;
            }
            sqlite3_bind_text(st.stmt, 1, eos_id.c_str(), static_cast<int>(eos_id.size()), SQLITE_TRANSIENT);

            int rc;
            while ((rc = sqlite3_step(st.stmt)) == SQLITE_ROW)
            {
                const unsigned char* name = sqlite3_column_text(st.stmt, 0);
                const unsigned char* species = sqlite3_column_text(st.stmt, 1);
                if (name)
                    out.push_back({reinterpret_cast<const char*>(name),
                                   species ? reinterpret_cast<const char*>(species) : ""});
            }
            if (rc != SQLITE_DONE)
            {
                error = sqlite3_errmsg(db.handle);
                return false;
            }
            return true;
        }

        bool Delete(const std::string& eos_id, const std::string& name, bool& deleted, std::string& error) override
        {
            deleted = false;
            Db db(s_.sqlite_path);
            if (!db)
            {
                error = db.error;
                return false;
            }

            Stmt st(db.handle, "DELETE FROM \"" + s_.table + "\" WHERE eos_id = ? AND template_name = ?");
            if (!st.ok())
            {
                error = sqlite3_errmsg(db.handle);
                return false;
            }
            sqlite3_bind_text(st.stmt, 1, eos_id.c_str(), static_cast<int>(eos_id.size()), SQLITE_TRANSIENT);
            sqlite3_bind_text(st.stmt, 2, name.c_str(), static_cast<int>(name.size()), SQLITE_TRANSIENT);

            if (sqlite3_step(st.stmt) != SQLITE_DONE)
            {
                error = sqlite3_errmsg(db.handle);
                return false;
            }
            deleted = sqlite3_changes(db.handle) > 0;
            return true;
        }

    private:
        DbSettings s_;
    };
}

std::unique_ptr<IBackend> MakeSqliteBackend(const DbSettings& settings)
{
    return std::make_unique<SqliteBackend>(settings);
}
