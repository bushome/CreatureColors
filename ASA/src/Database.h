#pragma once

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

// Sentinel stored in a region column when the species doesn't use that region.
constexpr uint8_t kUnusedRegion = 255;

struct DbSettings
{
    std::string host;
    unsigned int port = 3306;
    std::string user;
    std::string password;
    std::string database;
    std::string table;
};

struct ColorTemplate
{
    std::string species;                // blueprint path of the creature class
    std::array<uint8_t, 6> regions{};   // 255 = region unused by species
};

enum class InsertResult
{
    Inserted,
    AlreadyExists,
    Error
};

// All MariaDB work runs on one background thread so the game thread never waits on the network.
// A connection is opened per request and closed right after, so nothing is held open while idle.
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

private:
    using Continuation = std::function<void()>;
    using Work = std::function<Continuation()>;

    void Enqueue(Work work);
    void WorkerLoop();

    DbSettings settings_;
    std::thread worker_;
    std::mutex work_mutex_;
    std::condition_variable work_cv_;
    std::deque<Work> work_;
    bool stopping_ = false;

    std::mutex done_mutex_;
    std::deque<Continuation> done_;
};
