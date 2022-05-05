#include "thread_pool.hpp"
#include <chrono>
#include <functional>
#include <queue>
#include <utility>

class TimerPool final {
  private:
	class TimerTask {
	  public:
		std::function<void()> task;
		std::chrono::steady_clock::time_point time;
		std::optional<std::chrono::milliseconds> period;

		TimerTask() = default;

		TimerTask(std::function<void()> task,
				  std::chrono::steady_clock::time_point time,
				  std::optional<std::chrono::milliseconds> period =
					  std::nullopt) noexcept
			: task(std::move(task)), time(time), period(period) {}

		[[nodiscard]] auto is_higher_priority(const TimerTask &other) const
			-> bool {
			return this->time < other.time;
		}
	};

	std::mutex tasks_mtx;
	std::priority_queue<TimerTask, std::vector<TimerTask>,
						bool (*)(const TimerTask &, const TimerTask &)>
		tasks{[](const TimerTask &lhs, const TimerTask &rhs) {
			// The priority queue would place the rhs task at the top of the
			// queue if this predicate is true
			return rhs.is_higher_priority(lhs);
		}};
	ThreadPool &thread_pool;
	std::binary_semaphore work_semaphore;
	std::thread scheduling_thread;
	bool terminate_pool = false;

	void run_scheduling_thread() {
		auto wait_abs_time = std::chrono::steady_clock::time_point::max();

		auto is_work_ready = [this] {
			auto task_is_ready =
				!tasks.empty() &&
				tasks.top().time <= std::chrono::steady_clock::now();
			return terminate_pool || task_is_ready;
		};

		auto update_wait_abs_time = [this, &wait_abs_time] {
			if (tasks.empty()) {
				wait_abs_time = std::chrono::steady_clock::time_point::max();
			} else {
				wait_abs_time = tasks.top().time;
			}
		};

		while (true) {
			TimerTask timer_task;

			// Check if there's work to be done
			std::ignore = work_semaphore.try_acquire_until(wait_abs_time);
			{
				std::lock_guard lock{tasks_mtx};
				if (terminate_pool) {
					return;
				}

				// Check if there's work to be done
				if (!is_work_ready()) {
					update_wait_abs_time();
					continue;
				}

				timer_task = tasks.top();
				tasks.pop();

				// Update the wait_abs_time after popping the top task
				update_wait_abs_time();
			}

			// Push the task to be executed on the thread pool
			thread_pool.push_task(timer_task.task);

			// If the task has a period, push it back to the queue
			if (timer_task.period.has_value()) {
				auto new_wait_time =
					timer_task.time + timer_task.period.value();

				// Update the wait_abs_time if needed
				wait_abs_time = std::min(wait_abs_time, new_wait_time);

				{
					std::lock_guard tasks_lock{tasks_mtx};
					tasks.emplace(timer_task.task, new_wait_time,
								  timer_task.period);
				}
			}
		};
	}

	void notify_scheduling_thread() {
		work_semaphore.release();
	}

  public:
	TimerPool(ThreadPool &thread_pool)
		: thread_pool(thread_pool), work_semaphore(0) {
		scheduling_thread =
			std::thread(&TimerPool::run_scheduling_thread, this);
	};

	// delete copy and move constructors and assign operators
	TimerPool(const TimerPool &) = delete;
	auto operator=(const TimerPool &) -> TimerPool & = delete;
	TimerPool(TimerPool &&) = delete;
	auto operator=(TimerPool &&) -> TimerPool & = delete;

	template <typename F, typename... ArgTypes,
			  typename R = typename std::invoke_result<F, ArgTypes...>::type>
	auto push_task_once(std::chrono::steady_clock::time_point time,
						const F &task, ArgTypes... args) -> std::future<R> {

		// https://stackoverflow.com/questions/25330716/move-only-version-of-stdfunction
		// on why this is shared_ptr instead of unique_ptr
		auto promise = std::make_shared<std::promise<R>>();

		auto fut = promise->get_future();

		{
			std::lock_guard<std::mutex> tasks_lock_guard{tasks_mtx};
			tasks.emplace(
				[promise = std::move(promise), task,
				 ... args = std::forward<ArgTypes>(args)]() mutable {
					if constexpr (std::is_same<R, void>::value) {
						task(args...);
						promise->set_value();
					} else {
						promise->set_value(task(args...));
					}
				},
				time);
		}

		this->notify_scheduling_thread();
		return fut;
	}

	template <typename F, typename... ArgTypes>
	auto push_task_periodic(std::chrono::steady_clock::time_point time,
							std::chrono::milliseconds period, const F &task,
							ArgTypes... args) {
		{
			std::lock_guard<std::mutex> tasks_lock_guard{tasks_mtx};
			tasks.emplace([task = std::move(task),
						   ... args = std::forward<ArgTypes>(
							   args)]() mutable { task(args...); },
						  time, period);
		}

		this->notify_scheduling_thread();
	}

	~TimerPool() {
		terminate_pool = true;
		this->notify_scheduling_thread();
		scheduling_thread.join();
	}
};
