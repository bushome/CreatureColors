#pragma once

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Sentinel stored in a region column when the species doesn't use that region.
constexpr uint8_t kUnusedRegion = 255;

struct DbSettings
{
    bool use_mysql = true;

    // MariaDB / MySQL
    std::string host;
    unsigned int port = 3306;
    std::string user;
    std::string password;
    std::string database;

    // SQLite: full path to the database file (its folder is created if missing)
    std::string sqlite_path;

    // Both engines
    std::string table;
};

struct ColorTemplate
{
    std::string species;                // blueprint path of the creature class
    std::array<uint8_t, 6> regions{};   // 255 = region unused by species
};

// One row of a player's template list.
struct TemplateEntry
{
    std::string name;
    std::string species; // blueprint path, same value as ColorTemplate::species
};

enum class InsertResult
{
    Inserted,
    AlreadyExists,
    Error
};

// One storage engine. Every call opens its own connection and closes it before returning,
// so nothing stays open while the plugin is idle. Only ever called from the worker thread.
class IBackend
{
public:
    virtual ~IBackend() = default;
    virtual bool EnsureTable(std::string& error) = 0;
    virtual InsertResult InsertIfAbsent(const std::string& eos_id, const std::string& name,
                                        const ColorTemplate& tpl, std::string& error) = 0;
    virtual bool Upsert(const std::string& eos_id, const std::string& name,
                        const ColorTemplate& tpl, std::string& error) = 0;
    // ok=true with an empty optional means "no such template".
    virtual bool Fetch(const std::string& eos_id, const std::string& name,
                       std::optional<ColorTemplate>& out, std::string& error) = 0;
    // All of one player's templates (name + species). Order is not relied on; the caller sorts.
    virtual bool List(const std::string& eos_id, std::vector<TemplateEntry>& out, std::string& error) = 0;
    // deleted=false (with ok=true) means there was no such template.
    virtual bool Delete(const std::string& eos_id, const std::string& name, bool& deleted, std::string& error) = 0;
};

std::unique_ptr<IBackend> MakeMysqlBackend(const DbSettings& settings);
std::unique_ptr<IBackend> MakeSqliteBackend(const DbSettings& settings);

// All storage work runs on one background thread so the game thread never waits on disk or network.
// Completion callbacks are queued back and run on the game thread from Pump().
class Database
{
public:
    void Start(const DbSettings& settings);
    void Stop();

    // Call from the game thread (OnTick). Runs completed-request callbacks.
    void Pump();

    void EnsureTable(std::function<void(bool ok, std::string error)> done);

    // Inserts only if (owner, name) doesn't exist yet. AlreadyExists means nothing was written.
    void InsertIfAbsent(const std::string& eos_id, const std::string& name, const ColorTemplate& tpl,
                        std::function<void(InsertResult, std::string error)> done);

    // Inserts or overwrites the existing row for (owner, name).
    void Upsert(const std::string& eos_id, const std::string& name, const ColorTemplate& tpl,
                std::function<void(bool ok, std::string error)> done);

    void Fetch(const std::string& eos_id, const std::string& name,
               std::function<void(bool ok, std::optional<ColorTemplate>, std::string error)> done);

    void List(const std::string& eos_id,
              std::function<void(bool ok, std::vector<TemplateEntry> entries, std::string error)> done);

    void Delete(const std::string& eos_id, const std::string& name,
                std::function<void(bool ok, bool deleted, std::string error)> done);

private:
    using Continuation = std::function<void()>;
    using Work = std::function<Continuation()>;

    void Enqueue(Work work);
    void WorkerLoop();

    std::unique_ptr<IBackend> backend_;
    std::thread worker_;
    std::mutex work_mutex_;
    std::condition_variable work_cv_;
    std::deque<Work> work_;
    bool stopping_ = false;

    std::mutex done_mutex_;
    std::deque<Continuation> done_;
};
