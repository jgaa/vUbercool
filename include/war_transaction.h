#pragma once

#include <atomic>
#include <thread>
#include <log/WarLog.h>

namespace war {

/*! Simple transaction class
 *
 * Designed to undo partially executed transactionss
 * during stack unwinding.
 */
class Transaction
{
public:
    using undo_func_t = std::function<void()>;

    Transaction() = default;

    ~Transaction() {
        if (!committed_) {
            LOG_WARN_FN << "Rolling back non-committed transaction!";
            for(auto undo : undoers_) {
                undo();
            }
        }
    }

    /*! Flag the transaction as complete.
     *
     * If this method is called, the undo operations are
     * not invoked.
     */
    void Commit() {
        committed_ = true;
    }

    /*! Add operartion to perform during rollback.
     *      Operations are invoked in the reverse order that they are added.
     */
    void AddUndo(undo_func_t undo) {
        //std::lock_guard<std::mutex> lock(mutex_);
        undoers_.insert(undoers_.begin(), undo);
    }

private:
    std::vector<undo_func_t> undoers_;
    //std::atomic_bool committed_;
    //std::mutex mutex_;
    bool committed_ {false};

};

} // namespace war
