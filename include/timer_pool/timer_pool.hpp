#include "thread_pool/thread_pool.hpp"
#include <chrono>
#include <functional>
#include <queue>
#include <utility>

namespace timer_pool {

class TimerPool final {
  public:
	TimerPool(thread_pool::ThreadPool &thread_pool) : thread_pool(thread_pool) {
		scheduling_thread =
			std::thread(&TimerPool::run_scheduling_thread, this);
	};

	// delete copy and move constructors and assign operators
	TimerPool(const TimerPool &) = delete;
	auto operator=(const TimerPool &) -> TimerPool & = delete;
	TimerPool(TimerPool &&) = delete;
	auto operator=(TimerPool &&) -> TimerPool & = delete;

	template <typename F, typename... Args,
			  typename R = typename std::invoke_result<F, Args...>::type>
	auto push_task_once(std::chrono::steady_clock::time_point time,
						const F &&task, Args &&...args) -> std::future<R> {

		auto packaged_task = std::make_shared<std::packaged_task<R()>>(
			[task = std::forward<F>(task),
			 ... args = std::forward<Args>(args)] { return task(args...); });

		auto fut = packaged_task->get_future();

		tasks.getScopedAccessor()->emplace_task(
			[packaged_task = std::move(packaged_task)] { (*packaged_task)(); },
			time);

		this->notify_scheduling_thread();
		return fut;
	}

	template <typename F, typename... ArgTypes>
	auto push_task_periodic(std::chrono::steady_clock::time_point time,
							std::chrono::milliseconds period, const F &&task,
							ArgTypes &&...args) {

		tasks.getScopedAccessor()->emplace_task(
			[task = std::forward<F>(task),
			 ... args = std::forward<ArgTypes>(args)]() mutable {
				task(args...);
			},
			time, period);

		this->notify_scheduling_thread();
	}

	~TimerPool() {
		terminate_pool = true;
		this->notify_scheduling_thread();
		scheduling_thread.join();
	}

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

	class TimerQueue {
	  public:
		std::priority_queue<TimerTask, std::vector<TimerTask>,
							bool (*)(const TimerTask &, const TimerTask &)>
			queue{[](const TimerTask &lhs, const TimerTask &rhs) {
				// The priority queue would place the rhs task at the top of the
				// queue if this predicate is true
				return rhs.is_higher_priority(lhs);
			}};
		std::chrono::time_point<std::chrono::steady_clock> wait_time_abs =
			std::chrono::steady_clock::time_point::max();

		template <typename... Args> auto emplace_task(Args &&...args) {
			queue.emplace(std::forward<Args>(args)...);
			update_wait_abs_time();
		};

		auto pop_task() -> TimerTask {
			auto result = queue.top();
			queue.pop();
			update_wait_abs_time();
			return result;
		}

		[[nodiscard]] auto is_task_ready() const -> bool {
			return !queue.empty() &&
				   queue.top().time <= std::chrono::steady_clock::now();
		}

	  private:
		auto update_wait_abs_time() -> void {
			if (queue.empty()) {
				wait_time_abs = std::chrono::steady_clock::time_point::max();
			} else {
				wait_time_abs = queue.top().time;
			}
		};
	};
	thread_pool::MutexWrapper<TimerQueue> tasks;

	thread_pool::ThreadPool &thread_pool;

	std::condition_variable work_condition_variable;

	std::thread scheduling_thread;

	bool terminate_pool = false;

	void run_scheduling_thread() {
		while (true) {
			TimerTask timer_task;

			// Check if there's work to be done
			{
				std::unique_lock lock(tasks.getMutex());
				work_condition_variable.wait_until(
					lock, tasks.getUnsafeAccessor().wait_time_abs);

				if (terminate_pool) {
					return;
				}

				if (!tasks.getUnsafeAccessor().is_task_ready()) {
					continue;
				}

				timer_task = tasks.getUnsafeAccessor().pop_task();
			}

			// Push the task to be executed on the thread pool
			thread_pool.push_task(timer_task.task);

			// If the task has a period, push it back to the queue
			if (timer_task.period.has_value()) {
				auto new_wait_time =
					timer_task.time + timer_task.period.value();

				tasks.getScopedAccessor()->emplace_task(
					timer_task.task, new_wait_time, timer_task.period);
			}
		};
	}

	void notify_scheduling_thread() {
		work_condition_variable.notify_one();
	}
};

} // namespace timer_pool
