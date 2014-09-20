
#include "vUbercool.h"
#include "Statistics.h"
#include "log/WarLog.h"
#include "war_error_handling.h"

using namespace std;
using namespace war;

namespace vUbercool {
namespace impl {

class StatisticsManagerImpl : public StatisticsManager
{
public:
    StatisticsManagerImpl(Pipeline& pipeline, int logIntervalSeconds)
    : pipeline_ {pipeline}, log_interval_in_seconds_{logIntervalSeconds}
    {
        WAR_ASSERT(instance_ == nullptr);
        instance_ = this;
        if (log_interval_in_seconds_ > 0) {
            ScheduleLogUpdate();
        }
    }

    ~StatisticsManagerImpl() {
        WAR_ASSERT(instance_ == this);
        instance_ = nullptr;
    }

    virtual void AddStat(StatType stype, const Statistics& stats) override {
        pipeline_.Dispatch(task_t{[this, stype, stats](){
            stats_[Ix(stype)] += stats;
        }, "Adding stats"});
    }

    void GetStats(StatType stype, getstats_t callback) override {
        pipeline_.Dispatch(task_t{[this, stype, callback]() {
            const auto mystats = stats_[Ix(stype)];
            pipeline_.Post(task_t{[mystats, callback]() {
                try {
                    callback(mystats);
                } WAR_CATCH_ERROR;

            }, "Returning stats"});
        }, "Fetching stats"});
    }

    static StatisticsManager& GetManager() {
        /* This implementation is not thread-safe. But with an expected
         * use-pattern, where the manager is initialized early and
         * disposed late, this will not matter.
         */
        if (instance_ == nullptr) {
            WAR_THROW_T(ExceptionNotFound, "StatisticsManager instance");
        }
        return *instance_;
    }

private:
    int Ix(StatType st) const { return static_cast<int>(st); }

    void ScheduleLogUpdate() {
        pipeline_.PostWithTimer(task_t{[this]() {
            static const char *stat_names[] = {"HTTP", "DNS"};

            for(size_t i = 0; i < stats_.size(); ++i) {

                LOG_NOTICE << "Statistics " << stat_names[i] << ':'
                    << " connections=" << stats_[i].connections
                    << ", failed_connections=" << stats_[i].failed_connections
                    << ", ok_requests=" << stats_[i].ok_requests
                    << ", failed_requests=" << stats_[i].failed_requests
                    << ", bad_requests=" << stats_[i].bad_requests
                    << ", timeout_dropped_connections=" << stats_[i].timeout_dropped_connections
                    << ", overflow_dropped_connections=" << stats_[i].overflow_dropped_connections
                    << ", bytes_received=" << stats_[i].bytes_received
                    << ", bytes_sent=" << stats_[i].bytes_sent
                    << ", unknown_names=" << stats_[i].unknown_names;
            }

            ScheduleLogUpdate();
        }, "Periodic Log Statistics"}, log_interval_in_seconds_ * 1000);
    }

    array<Statistics, 2> stats_;
    Pipeline& pipeline_;
    const int log_interval_in_seconds_ {0};
    static StatisticsManagerImpl *instance_;
};

StatisticsManagerImpl *StatisticsManagerImpl::instance_;


} // impl


StatisticsManager& StatisticsManager::GetManager() {
    return impl::StatisticsManagerImpl::GetManager();
}

StatisticsManager::ptr_t
StatisticsManager::Create(war::Threadpool& pool, int logIntervalSeconds)
{
    ptr_t instance {new impl::StatisticsManagerImpl(pool.GetPipeline(0),
                                                    logIntervalSeconds)};

    return instance;
}

} // vUbercool
