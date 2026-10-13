#include "Database.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include <mysql.h>

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

    std::string InsertColumns(const std::string& table)
    {
        return "INSERT INTO `" + table + "` (`eos_id`, `template_name`, `species_class`, "
               "`region_0`, `region_1`, `region_2`, `region_3`, `region_4`, `region_5`) "
               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    }

    // Runs a prepared INSERT. Returns affected rows, or -1 on error.
    long long ExecuteInsert(MYSQL* conn, const std::string& sql, TemplateParams& params, std::string& error)
    {
        Statement st(conn);
        if (!st.stmt)
        {
            error = "mysql_stmt_init failed";
            return -1;
        }
        if (mysql_stmt_prepare(st.stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0 ||
            mysql_stmt_bind_param(st.stmt, params.bind) != 0 ||
            mysql_stmt_execute(st.stmt) != 0)
        {
            error = mysql_stmt_error(st.stmt);
            return -1;
        }
        return static_cast<long long>(mysql_stmt_affected_rows(st.stmt));
    }
}

void Database::Start(const DbSettings& settings)
{
    settings_ = settings;
    stopping_ = false;
    mysql_library_init(0, nullptr, nullptr);
    worker_ = std::thread(&Database::WorkerLoop, this);
}

void Database::Stop()
{
    {
        std::lock_guard<std::mutex> lock(work_mutex_);
        stopping_ = true;
    }
    work_cv_.notify_all();
    if (worker_.joinable())
        worker_.join();

    std::lock_guard<std::mutex> lock(done_mutex_);
    done_.clear();
}

void Database::Pump()
{
    std::deque<Continuation> ready;
    {
        std::lock_guard<std::mutex> lock(done_mutex_);
        if (done_.empty())
            return;
        ready.swap(done_);
    }
    for (auto& fn : ready)
    {
        if (fn)
            fn();
    }
}

void Database::Enqueue(Work work)
{
    {
        std::lock_guard<std::mutex> lock(work_mutex_);
        if (stopping_)
            return;
        work_.push_back(std::move(work));
    }
    work_cv_.notify_one();
}

void Database::WorkerLoop()
{
    mysql_thread_init();

    for (;;)
    {
        Work job;
        {
            std::unique_lock<std::mutex> lock(work_mutex_);
            work_cv_.wait(lock, [this] { return stopping_ || !work_.empty(); });
            if (stopping_)
                break;
            job = std::move(work_.front());
            work_.pop_front();
        }

        Continuation cont = job();
        if (cont)
        {
            std::lock_guard<std::mutex> lock(done_mutex_);
            done_.push_back(std::move(cont));
        }
    }

    mysql_thread_end();
}

void Database::EnsureTable(std::function<void(bool, std::string)> done)
{
    const DbSettings s = settings_;
    Enqueue([s, done]() -> Continuation {
        Connection conn(s);
        if (!conn)
            return [done, err = conn.error] { done(false, err); };

        const std::string sql =
            "CREATE TABLE IF NOT EXISTS `" + s.table + "` ("
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
            return [done, err = std::string(mysql_error(conn.handle))] { done(false, err); };

        return [done] { done(true, {}); };
    });
}

void Database::InsertIfAbsent(const std::string& eos_id, const std::string& name, const ColorTemplate& tpl,
                              std::function<void(InsertResult, std::string)> done)
{
    const DbSettings s = settings_;
    Enqueue([s, eos_id, name, tpl, done]() -> Continuation {
        Connection conn(s);
        if (!conn)
            return [done, err = conn.error] { done(InsertResult::Error, err); };

        // A plain INSERT fails with ER_DUP_ENTRY (1062) on the unique key, which tells us the
        // template already exists without a separate SELECT and without INSERT IGNORE hiding other errors.
        TemplateParams params(eos_id, name, tpl);
        Statement st(conn.handle);
        const std::string sql = InsertColumns(s.table);
        if (!st.stmt)
            return [done] { done(InsertResult::Error, "mysql_stmt_init failed"); };

        if (mysql_stmt_prepare(st.stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0 ||
            mysql_stmt_bind_param(st.stmt, params.bind) != 0)
            return [done, err = std::string(mysql_stmt_error(st.stmt))] { done(InsertResult::Error, err); };

        if (mysql_stmt_execute(st.stmt) != 0)
        {
            if (mysql_stmt_errno(st.stmt) == 1062)
                return [done] { done(InsertResult::AlreadyExists, {}); };
            return [done, err = std::string(mysql_stmt_error(st.stmt))] { done(InsertResult::Error, err); };
        }

        return [done] { done(InsertResult::Inserted, {}); };
    });
}

void Database::Upsert(const std::string& eos_id, const std::string& name, const ColorTemplate& tpl,
                      std::function<void(bool, std::string)> done)
{
    const DbSettings s = settings_;
    Enqueue([s, eos_id, name, tpl, done]() -> Continuation {
        Connection conn(s);
        if (!conn)
            return [done, err = conn.error] { done(false, err); };

        const std::string sql = InsertColumns(s.table) +
            " ON DUPLICATE KEY UPDATE `species_class` = VALUES(`species_class`),"
            " `region_0` = VALUES(`region_0`), `region_1` = VALUES(`region_1`),"
            " `region_2` = VALUES(`region_2`), `region_3` = VALUES(`region_3`),"
            " `region_4` = VALUES(`region_4`), `region_5` = VALUES(`region_5`)";

        TemplateParams params(eos_id, name, tpl);
        std::string error;
        if (ExecuteInsert(conn.handle, sql, params, error) < 0)
            return [done, error] { done(false, error); };

        return [done] { done(true, {}); };
    });
}

void Database::Fetch(const std::string& eos_id, const std::string& name,
                     std::function<void(bool, std::optional<ColorTemplate>, std::string)> done)
{
    const DbSettings s = settings_;
    Enqueue([s, eos_id, name, done]() -> Continuation {
        Connection conn(s);
        if (!conn)
            return [done, err = conn.error] { done(false, std::nullopt, err); };

        const std::string sql =
            "SELECT `species_class`, `region_0`, `region_1`, `region_2`, `region_3`, `region_4`, `region_5` "
            "FROM `" + s.table + "` WHERE `eos_id` = ? AND `template_name` = ? LIMIT 1";

        Statement st(conn.handle);
        if (!st.stmt)
            return [done] { done(false, std::nullopt, "mysql_stmt_init failed"); };

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
            return [done, err = std::string(mysql_stmt_error(st.stmt))] { done(false, std::nullopt, err); };
        }

        const int rc = mysql_stmt_fetch(st.stmt);
        if (rc == MYSQL_NO_DATA)
            return [done] { done(true, std::nullopt, {}); };
        if (rc != 0 && rc != MYSQL_DATA_TRUNCATED)
            return [done, err = std::string(mysql_stmt_error(st.stmt))] { done(false, std::nullopt, err); };

        ColorTemplate tpl;
        tpl.species.assign(species_buf, std::min<size_t>(species_len, sizeof(species_buf)));
        tpl.regions = regions;
        return [done, tpl] { done(true, tpl, {}); };
    });
}
