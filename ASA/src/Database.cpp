#include "Database.h"

void Database::Start(const DbSettings& settings)
{
    backend_ = settings.use_mysql ? MakeMysqlBackend(settings) : MakeSqliteBackend(settings);
    stopping_ = false;
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

    backend_.reset();

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
}

void Database::EnsureTable(std::function<void(bool, std::string)> done)
{
    Enqueue([this, done]() -> Continuation {
        std::string error;
        const bool ok = backend_->EnsureTable(error);
        return [done, ok, error] { done(ok, error); };
    });
}

void Database::InsertIfAbsent(const std::string& eos_id, const std::string& name, const ColorTemplate& tpl,
                              std::function<void(InsertResult, std::string)> done)
{
    Enqueue([this, eos_id, name, tpl, done]() -> Continuation {
        std::string error;
        const InsertResult result = backend_->InsertIfAbsent(eos_id, name, tpl, error);
        return [done, result, error] { done(result, error); };
    });
}

void Database::Upsert(const std::string& eos_id, const std::string& name, const ColorTemplate& tpl,
                      std::function<void(bool, std::string)> done)
{
    Enqueue([this, eos_id, name, tpl, done]() -> Continuation {
        std::string error;
        const bool ok = backend_->Upsert(eos_id, name, tpl, error);
        return [done, ok, error] { done(ok, error); };
    });
}

void Database::Fetch(const std::string& eos_id, const std::string& name,
                     std::function<void(bool, std::optional<ColorTemplate>, std::string)> done)
{
    Enqueue([this, eos_id, name, done]() -> Continuation {
        std::string error;
        std::optional<ColorTemplate> tpl;
        const bool ok = backend_->Fetch(eos_id, name, tpl, error);
        return [done, ok, tpl, error] { done(ok, tpl, error); };
    });
}

void Database::List(const std::string& eos_id,
                    std::function<void(bool, std::vector<TemplateEntry>, std::string)> done)
{
    Enqueue([this, eos_id, done]() -> Continuation {
        std::string error;
        std::vector<TemplateEntry> entries;
        const bool ok = backend_->List(eos_id, entries, error);
        return [done, ok, entries = std::move(entries), error] { done(ok, entries, error); };
    });
}

void Database::Delete(const std::string& eos_id, const std::string& name,
                      std::function<void(bool, bool, std::string)> done)
{
    Enqueue([this, eos_id, name, done]() -> Continuation {
        std::string error;
        bool deleted = false;
        const bool ok = backend_->Delete(eos_id, name, deleted, error);
        return [done, ok, deleted, error] { done(ok, deleted, error); };
    });
}
