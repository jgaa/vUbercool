

#include "war_tasks.h" // PCH file
#include "tasks/WarPipeline.h"
#include "log/WarLog.h"
#include "war_debug_helper.h"

using namespace std;
using namespace war;

std::ostream& operator << (std::ostream& o, const war::task_t& task)
{
	return o << log::Esc(task.second);
}

war::Pipeline::Pipeline(const string &name,
                        int id,
                        const std::size_t capacity)
: io_service_{ new boost::asio::io_service }, name_{ name }, closed_{false},
    capacity_{ capacity }, count_{0}, closing_{false}, id_{id}
{
	WAR_LOG_FUNCTION;
	LOG_TRACE3_F_FN(log::LA_THREADS) << log::Esc(name_);

	my_sync_t sync;
	thread_.reset(new thread(&war::Pipeline::Run, this, ref(sync)));

    // Wait for the thread to run so that we can re-throw any early exceptions
	sync.get_future().get();
}

war::Pipeline::~Pipeline()
{
	WAR_LOG_FUNCTION;
	LOG_TRACE3_F_FN(log::LA_THREADS) << log::Esc(name_);
	if (thread_ && thread_->joinable()) {
		LOG_TRACE3_F_FN(log::LA_THREADS) << log::Esc(name_) << "Joining";
		thread_->join();
		LOG_TRACE3_F_FN(log::LA_THREADS) << log::Esc(name_) << "Joined";
	}
}

void war::Pipeline::Run(my_sync_t & sync)
{
	WAR_LOG_FUNCTION;

	unique_lock<std::mutex> waiter_lock(waiter_);

	try {
		debug::SetThreadName(name_);
		work_.reset(new boost::asio::io_service::work(*io_service_));
	}
	catch (...) {
		LOG_ERROR_FN << "Caught exception! Aborting this operation!";
		sync.set_exception(current_exception());
		return;
	}
	// Makes sure that we got here before the original thread can leave the constructor.
	sync.set_value();

	LOG_DEBUG_F_FN(log::LA_THREADS) << "Starting Pipeline thread loop " << log::Esc(name_);
	try {
		io_service_->run();
	} WAR_CATCH_ALL_E;

	LOG_DEBUG_F_FN(log::LA_THREADS) << "Ending Pipeline thread loop " << log::Esc(name_)
        << ". The total number of tasks run was " << tasks_run_ << ".";
}

void war::Pipeline::Post(const func_t &fn, const char * name)
{
	WAR_LOG_FUNCTION;
	Post(task_t{fn, name });
}

void war::Pipeline::Post(func_t &&fn, const char * name)
{
	WAR_LOG_FUNCTION;
	Post(task_t{ fn, move(name) });
}

void war::Pipeline::Post(task_t &&task)
{
	WAR_LOG_FUNCTION;

	if (closing_) {

		LOG_WARN_FN << "The pipeline " << log::Esc(name_)
			<< " is closing. Task dismissed: " << task;
		return;
	}

	LOG_TRACE3_F_FN(log::LA_THREADS) << "Posting [move] task on Pipeline " << task;

	AddingTask();
	io_service_->post(bind(&war::Pipeline::ExecTask_, this, task, true));
}

void war::Pipeline::Post(const task_t &task)
{
	WAR_LOG_FUNCTION;
	Post(move(task_t(task)));
}

void war::Pipeline::Dispatch(const func_t &fn, const char * name)
{
	WAR_LOG_FUNCTION;
	Dispatch(task_t{ fn, name });
}

void war::Pipeline::Dispatch(func_t &&fn, const char * name)
{
	WAR_LOG_FUNCTION;
	Dispatch(task_t{ move(fn), move(name) });
}

void war::Pipeline::Dispatch(const task_t &task)
{
	WAR_LOG_FUNCTION;
	Dispatch(move(task_t(task)));
}

void war::Pipeline::Dispatch(task_t &&task)
{
	WAR_LOG_FUNCTION;

	if (closing_) {

		LOG_WARN_FN << "The pipeline " << log::Esc(name_)
			<< " is closing. Task dismissed: " << task;
		return;
	}

	LOG_TRACE3_F_FN(log::LA_THREADS) << "Dispatching [move] task on Pipeline " <<task;

	if (IsPipelineThread()) {
		ExecTask_(task, false);
	}
	else {
		Post(task);
	}
}

void war::Pipeline::PostWithTimer(const func_t& fn, const char * name,
	const std::uint32_t milliSeconds)
{
	WAR_LOG_FUNCTION;
	PostWithTimer(task_t{ fn, name }, milliSeconds);
}

void war::Pipeline::PostWithTimer(func_t&& fn, const char * name,
	const std::uint32_t milliSeconds)
{
	WAR_LOG_FUNCTION;
	PostWithTimer(task_t{ move(fn), move(name) }, milliSeconds);
}

void war::Pipeline::PostWithTimer(const task_t &task,
	const uint32_t milliSeconds)
{
	WAR_LOG_FUNCTION;
	task_t my_task{ task };
	PostWithTimer(move(my_task), milliSeconds);
}

void war::Pipeline::PostWithTimer(task_t &&task,
	const uint32_t milliSeconds)
{
	WAR_LOG_FUNCTION;

	if (closing_) {

		LOG_WARN_FN << "The pipeline " << log::Esc(name_)
			<< " is closing. Task dismissed: " << task;
		return;
	}

	LOG_TRACE3_F_FN(log::LA_THREADS) << "Posting task " << task << " on Pipeline "
		<< " for execution in " << milliSeconds << " milliseconds.";

	timer_t timer(new boost::asio::deadline_timer(*io_service_));
	timer->expires_from_now(boost::posix_time::milliseconds(milliSeconds));
	timer->async_wait(bind(&war::Pipeline::OnTimer_, this, timer, task, placeholders::_1));
}


void war::Pipeline::Close()
{
	WAR_LOG_FUNCTION;

	if (closing_)
		return;

	if (io_service_) {
		LOG_TRACE3_F_FN(log::LA_THREADS) << "Posting Close on Pipeline ";
		io_service_->dispatch([this] {
			if (!closed_) {
				LOG_DEBUG << "Shutting down Pipeline " << log::Esc(name_);
				closed_ = true;
				work_.reset();
				io_service_->stop();
                LOG_DEBUG << "Finisheed shutting down Pipeline " << log::Esc(name_);
			}
		});
		closing_ = true;
	}
}

void war::Pipeline::OnTimer_(const timer_t& timer, const task_t &task, const boost::system::error_code& ec)
{
	WAR_LOG_FUNCTION;
	if (!ec) {
		ExecTask_(task, false);
	}
	else {
		LOG_DEBUG_FN << "Task " << task << " failed with error: " << ec;
	}
}

void war::Pipeline::AddingTask()
{
	if (++count_ > capacity_) {
		--count_;
		WAR_THROW_T(ExceptionCapacityExceeded,
			std::string("Out of capicity in Pipeline \"") + name_ + "\"");
	}
}

void war::Pipeline::ExecTask_(const task_t& task, bool counting)
{
	WAR_LOG_FUNCTION;

	if (counting) {
		--count_;
	}
	if (closing_) {
		LOG_DEBUG_FN << "Dismissing task " << task
			<< ". Pipeline " << log::Esc(name_)
			<< " is closed.";
        return;
	}
	LOG_TRACE3_F_FN(log::LA_THREADS) << "Executing task " << task;
	try {
		task.first();
        ++tasks_run_;
	} WAR_CATCH_ALL_E;
	LOG_TRACE3_F_FN(log::LA_THREADS) << "Finished executing task " << task;
}

void  war::Pipeline::WaitUntilClosed() const
{
	LOG_TRACE1_FN << "Waiting for waiter_" << " on Pipeline " << log::Esc(name_);
	lock_guard<std::mutex> lock{ waiter_ };
	LOG_TRACE1_FN << "Done waiting for waiter_" << " on Pipeline " << log::Esc(name_);
}
