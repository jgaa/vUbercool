#pragma once

#include <tasks/WarThreadpool.h>

namespace vUbercool {

struct Statistics
{
    using cnt_t = uint64_t;

    Statistics() {}

    Statistics(const Statistics& v)
    : connections {v.connections},
    failed_connections {v.failed_connections},
    ok_requests {v.ok_requests},
    failed_requests {v.failed_requests},
    bad_requests {v.bad_requests},
    timeout_dropped_connections {v.timeout_dropped_connections},
    overflow_dropped_connections {v.overflow_dropped_connections},
    bytes_received {v.bytes_received},
    bytes_sent {v.bytes_sent},
    unknown_names {v.unknown_names}
    {}

    cnt_t connections {0};
    cnt_t failed_connections {0};
    cnt_t ok_requests {0};
    cnt_t failed_requests {0};
    cnt_t bad_requests {0};
    cnt_t timeout_dropped_connections {0};
    cnt_t overflow_dropped_connections {0};
    cnt_t bytes_received {0};
    cnt_t bytes_sent {0};
    cnt_t unknown_names {0};

    Statistics& operator += (const Statistics& v) {
        connections += v.connections;
        failed_connections += v.failed_connections;
        ok_requests += v.ok_requests;
        failed_requests += v.failed_requests;
        bad_requests += v.bad_requests;
        timeout_dropped_connections += timeout_dropped_connections;
        overflow_dropped_connections += v.overflow_dropped_connections;
        bytes_received += v.bytes_received;
        bytes_sent += v.bytes_sent;
        unknown_names += v.unknown_names;

        return *this;
    }

    void Reset() {
        connections = 0;
        failed_connections = 0;
        ok_requests = 0;
        failed_requests = 0;
        bad_requests = 0;
        timeout_dropped_connections = 0;
        overflow_dropped_connections = 0;
        bytes_received = 0;
        bytes_sent = 0;
        unknown_names = 0;
    }
};

class StatisticsManager
{
public:
    enum class StatType { HTTP, DNS };
    using getstats_t = std::function<void (const Statistics&)>;
    using ptr_t = std::unique_ptr<StatisticsManager>;

    StatisticsManager() = default;
    virtual ~StatisticsManager() {}
    StatisticsManager(const StatisticsManager&) = delete;
    StatisticsManager& operator = (const StatisticsManager&) = delete;

    /*! Add statistics counters to the main statistics */
    virtual void AddStat(StatType stype, const Statistics& stats) = 0;

    /*! Get statistics */
    virtual void GetStats(StatType stype, getstats_t callback) = 0;

    /*! Get the instance of the statistics manager */
    static StatisticsManager& GetManager();

    static ptr_t Create(war::Threadpool& pool,
                                                     int logIntervalSeconds);
};

/*! Helper that updates the statistics based on the outcome of the request */
struct RequestStats
{
    enum class State { SUCESS, IGNORE, FAIL, BAD, UNSET };

    RequestStats(Statistics& stats) : stats_{stats} {}

    ~RequestStats() {

        switch(state) {
            case State::SUCESS:
                ++stats_.ok_requests;
                break;
            case State::IGNORE:
                // Do not update
                break;
            case State::FAIL:
            case State::UNSET:
                ++stats_.failed_requests;
                break;
            case State::BAD:
                ++stats_.bad_requests;
                break;
        }
    }

    State state {State::UNSET};
private:
    Statistics& stats_;
};

}
