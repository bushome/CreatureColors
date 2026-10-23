// MariaDB / MySQL storage (UseMysql = true).

#include "Database.h"

#include <algorithm>
#include <cstring>

#include <mysql/mysql.h> // vcpkg libmariadb installs its headers under include/mysql/

namespace
{
    struct Connection
    {
        MYSQL* handle = nullptr;
        std::string error;

        explicit Connection(const DbSettings& s)
        {
            handle = mysql_init(nullptr);
            if (!handle)
            {
                error = "mysql_init failed";
                return;
            }

            unsigned int timeout = 5;
            mysql_options(handle, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
            mysql_options(handle, MYSQL_OPT_READ_TIMEOUT, &timeout);
            mysql_options(handle, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
            mysql_options(handle, MYSQL_SET_CHARSET_NAME, "utf8mb4");

            // Connector/C 3.4+ requires TLS and verifies the server certificate by default, which fails
            // against a MariaDB server without TLS ("SSL is required, but the server does not support it").
            // Keep the plain connection CreatureColors has always used (same as Connector/C 3.3).
            my_bool enforce = 0;
            my_bool verify = 0;
            mysql_optionsv(handle, MYSQL_OPT_SSL_ENFORCE, &enforce);
            mysql_optionsv(handle, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify);

            if (!mysql_real_connect(handle, s.host.c_str(), s.user.c_str(), s.password.c_str(),
                                    s.database.c_str(), s.port, nullptr, 0))
            {
                error = mysql_error(handle);
                mysql_close(handle);
                handle = nullptr;
            }
        }

        ~Connection()
        {
            if (handle)
                mysql_close(handle);
        }

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        explicit operator bool() const { return handle != nullptr; }
    };

    struct Statement
    {
        MYSQL_STMT* stmt = nullptr;
        explicit Statement(MYSQL* conn) : stmt(mysql_stmt_init(conn)) {}
        ~Statement()
        {
            if (stmt)
                mysql_stmt_close(stmt);
        }
        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;
    };

    void BindString(MYSQL_BIND& b, const std::string& value, unsigned long& length)
    {
        length = static_cast<unsigned long>(value.size());
        b.buffer_type = MYSQL_TYPE_STRING;
        b.buffer = const_cast<char*>(value.data());
        b.buffer_length = length;
        b.length = &length;
    }

    void BindTiny(MYSQL_BIND& b, uint8_t& value)
    {
        b.buffer_type = MYSQL_TYPE_TINY;
        b.buffer = &value;
        b.is_unsigned = 1;
    }

    // Binds (eos_id, template_name, species_class, region_0..5) for INSERT statements.
    struct TemplateParams
    {
        MYSQL_BIND bind[9];
        unsigned long lengths[3]{};
        std::array<uint8_t, 6> regions{};

        TemplateParams(const std::string& eos_id, const std::string& name, const ColorTemplate& tpl)
            : regions(tpl.regions)
        {
            std::memset(bind, 0, sizeof(bind));
            BindString(bind[0], eos_id, lengths[0]);
            BindString(bind[1], name, lengths[1]);
            BindString(bind[2], tpl.species, lengths[2]);
            for (size_t i = 0; i < 6; ++i)
                BindTiny(bind[3 + i], regions[i]);
        }
    };

    std::string InsertSql(const std::string& table)
    {
        return "INSERT INTO `" + table + "` (`eos_id`, `template_name`, `species_class`, "
               "`region_0`, `region_1`, `region_2`, `region_3`, `region_4`, `region_5`) "
               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    }

    class MysqlBackend final : public IBackend
    {
    public:
        explicit MysqlBackend(const DbSettings& s) : s_(s)
        {
            mysql_library_init(0, nullptr, nullptr);
        }

        bool EnsureTable(std::string& error) override
        {
            Connection conn(s_);
            if (!conn)
            {
                error = conn.error;
                return false;
            }

            const std::string sql =
                "CREATE TABLE IF NOT EXISTS `" + s_.table + "` ("
                "`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,"
                "`eos_id` CHAR(32) NOT NULL,"
                "`template_name` VARCHAR(32) NOT NULL,"
                "`species_class` VARCHAR(255) NOT NULL,"
                "`region_0` TINYINT UNSIGNED NOT NULL,"
                "`region_1` TINYINT UNSIGNED NOT NULL,"
                "`region_2` TINYINT UNSIGNED NOT NULL,"
                "`region_3` TINYINT UNSIGNED NOT NULL,"
                "`region_4` TINYINT UNSIGNED NOT NULL,"
                "`region_5` TINYINT UNSIGNED NOT NULL,"
                "`created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "`updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
                "PRIMARY KEY (`id`),"
                "UNIQUE KEY `uq_owner_name` (`eos_id`, `template_name`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

            if (mysql_real_query(conn.handle, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
            {
                error = mysql_error(conn.handle);
                return false;
            }
            return true;
        }

        InsertResult InsertIfAbsent(const std::string& eos_id, const std::string& name,
                                    const ColorTemplate& tpl, std::string& error) override
        {
            Connection conn(s_);
            if (!conn)
            {
                error = conn.error;
                return InsertResult::Error;
            }

            // A plain INSERT fails with ER_DUP_ENTRY (1062) on the unique key, which tells us the
            // template already exists without a separate SELECT.
            TemplateParams params(eos_id, name, tpl);
            Statement st(conn.handle);
            const std::string sql = InsertSql(s_.table);
            if (!st.stmt)
            {
                error = "mysql_stmt_init failed";
                return InsertResult::Error;
            }
            if (mysql_stmt_prepare(st.stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0 ||
                mysql_stmt_bind_param(st.stmt, params.bind) != 0)
            {
                error = mysql_stmt_error(st.stmt);
                return InsertResult::Error;
            }
            if (mysql_stmt_execute(st.stmt) != 0)
            {
                if (mysql_stmt_errno(st.stmt) == 1062)
                    return InsertResult::AlreadyExists;
                error = mysql_stmt_error(st.stmt);
                return InsertResult::Error;
            }
            return InsertResult::Inserted;
        }

        bool Upsert(const std::string& eos_id, const std::string& name, const ColorTemplate& tpl,
                    std::string& error) override
        {
            Connection conn(s_);
            if (!conn)
            {
                error = conn.error;
                return false;
            }

            const std::string sql = InsertSql(s_.table) +
                " ON DUPLICATE KEY UPDATE `species_class` = VALUES(`species_class`),"
                " `region_0` = VALUES(`region_0`), `region_1` = VALUES(`region_1`),"
                " `region_2` = VALUES(`region_2`), `region_3` = VALUES(`region_3`),"
                " `region_4` = VALUES(`region_4`), `region_5` = VALUES(`region_5`)";

            TemplateParams params(eos_id, name, tpl);
            Statement st(conn.handle);
            if (!st.stmt)
            {
                error = "mysql_stmt_init failed";
                return false;
            }
            if (mysql_stmt_prepare(st.stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0 ||
                mysql_stmt_bind_param(st.stmt, params.bind) != 0 ||
                mysql_stmt_execute(st.stmt) != 0)
            {
                error = mysql_stmt_error(st.stmt);
                return false;
            }
            return true;
        }

        bool Fetch(const std::string& eos_id, const std::string& name,
                   std::optional<ColorTemplate>& out, std::string& error) override
        {
            out.reset();
            Connection conn(s_);
            if (!conn)
            {
                error = conn.error;
                return false;
            }

            const std::string sql =
                "SELECT `species_class`, `region_0`, `region_1`, `region_2`, `region_3`, `region_4`, `region_5` "
                "FROM `" + s_.table + "` WHERE `eos_id` = ? AND `template_name` = ? LIMIT 1";

            Statement st(conn.handle);
            if (!st.stmt)
            {
                error = "mysql_stmt_init failed";
                return false;
            }

            MYSQL_BIND params[2];
            std::memset(params, 0, sizeof(params));
            unsigned long param_len[2]{};
            BindString(params[0], eos_id, param_len[0]);
            BindString(params[1], name, param_len[1]);

            char species_buf[256]{};
            unsigned long species_len = 0;
            std::array<uint8_t, 6> regions{};

            MYSQL_BIND results[7];
            std::memset(results, 0, sizeof(results));
            results[0].buffer_type = MYSQL_TYPE_STRING;
            results[0].buffer = species_buf;
            results[0].buffer_length = sizeof(species_buf);
            results[0].length = &species_len;
            for (size_t i = 0; i < 6; ++i)
                BindTiny(results[1 + i], regions[i]);

            if (mysql_stmt_prepare(st.stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0 ||
                mysql_stmt_bind_param(st.stmt, params) != 0 ||
                mysql_stmt_execute(st.stmt) != 0 ||
                mysql_stmt_bind_result(st.stmt, results) != 0 ||
                mysql_stmt_store_result(st.stmt) != 0)
            {
                error = mysql_stmt_error(st.stmt);
                return false;
            }

            const int rc = mysql_stmt_fetch(st.stmt);
            if (rc == MYSQL_NO_DATA)
                return true;
            if (rc != 0 && rc != MYSQL_DATA_TRUNCATED)
            {
                error = mysql_stmt_error(st.stmt);
                return false;
            }

            ColorTemplate tpl;
            tpl.species.assign(species_buf, (std::min<size_t>)(species_len, sizeof(species_buf)));
            tpl.regions = regions;
            out = tpl;
            return true;
        }

        bool List(const std::string& eos_id, std::vector<TemplateEntry>& out, std::string& error) override
        {
            out.clear();
            Connection conn(s_);
            if (!conn)
            {
                error = conn.error;
                return false;
            }

            const std::string sql = "SELECT `template_name`, `species_class` FROM `" + s_.table +
                                    "` WHERE `eos_id` = ? ORDER BY `template_name`";
            Statement st(conn.handle);
            if (!st.stmt)
            {
                error = "mysql_stmt_init failed";
                return false;
            }

            MYSQL_BIND param[1];
            std::memset(param, 0, sizeof(param));
            unsigned long param_len = 0;
            BindString(param[0], eos_id, param_len);

            char name_buf[64]{};
            unsigned long name_len = 0;
            char species_buf[256]{};
            unsigned long species_len = 0;
            MYSQL_BIND result[2];
            std::memset(result, 0, sizeof(result));
            result[0].buffer_type = MYSQL_TYPE_STRING;
            result[0].buffer = name_buf;
            result[0].buffer_length = sizeof(name_buf);
            result[0].length = &name_len;
            result[1].buffer_type = MYSQL_TYPE_STRING;
            result[1].buffer = species_buf;
            result[1].buffer_length = sizeof(species_buf);
            result[1].length = &species_len;

            if (mysql_stmt_prepare(st.stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0 ||
                mysql_stmt_bind_param(st.stmt, param) != 0 ||
                mysql_stmt_execute(st.stmt) != 0 ||
                mysql_stmt_bind_result(st.stmt, result) != 0 ||
                mysql_stmt_store_result(st.stmt) != 0)
            {
                error = mysql_stmt_error(st.stmt);
                return false;
            }

            int rc;
            while ((rc = mysql_stmt_fetch(st.stmt)) == 0 || rc == MYSQL_DATA_TRUNCATED)
            {
                out.push_back({std::string(name_buf, (std::min<size_t>)(name_len, sizeof(name_buf))),
                               std::string(species_buf, (std::min<size_t>)(species_len, sizeof(species_buf)))});
            }

            if (rc != MYSQL_NO_DATA)
            {
                error = mysql_stmt_error(st.stmt);
                return false;
            }
            return true;
        }

        bool Delete(const std::string& eos_id, const std::string& name, bool& deleted, std::string& error) override
        {
            deleted = false;
            Connection conn(s_);
            if (!conn)
            {
                error = conn.error;
                return false;
            }

            const std::string sql = "DELETE FROM `" + s_.table + "` WHERE `eos_id` = ? AND `template_name` = ?";
            Statement st(conn.handle);
            if (!st.stmt)
            {
                error = "mysql_stmt_init failed";
                return false;
            }

            MYSQL_BIND params[2];
            std::memset(params, 0, sizeof(params));
            unsigned long lens[2]{};
            BindString(params[0], eos_id, lens[0]);
            BindString(params[1], name, lens[1]);

            if (mysql_stmt_prepare(st.stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0 ||
                mysql_stmt_bind_param(st.stmt, params) != 0 ||
                mysql_stmt_execute(st.stmt) != 0)
            {
                error = mysql_stmt_error(st.stmt);
                return false;
            }
            deleted = mysql_stmt_affected_rows(st.stmt) > 0;
            return true;
        }

    private:
        DbSettings s_;
    };
}

std::unique_ptr<IBackend> MakeMysqlBackend(const DbSettings& settings)
{
    return std::make_unique<MysqlBackend>(settings);
}
