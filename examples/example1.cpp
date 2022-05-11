#include "timer_pool/timer_pool.hpp"
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string_view>

const auto start_time = std::chrono::steady_clock::now();

auto print(std::string_view msg) -> void {
	static auto mtx = std::mutex();
	using namespace std::chrono;

	auto now = steady_clock::now();

	auto millisec_since_start =
		duration_cast<milliseconds>(now - start_time).count();
	auto seconds_since_start = duration_cast<seconds>(now - start_time).count();

	std::lock_guard<std::mutex> lock(mtx);
	std::cout << std::setfill('0') << std::setw(2) << seconds_since_start
			  << "s " << std::setw(3) << millisec_since_start % 1000 << "ms "
			  << msg << std::endl;
}

auto main() -> int {
	using namespace std::chrono_literals;

	thread_pool::ThreadPool thread_pool;
	timer_pool::TimerPool timer_pool{thread_pool};

	// std::this_thread::sleep_for(5ms);

	timer_pool.push_task_periodic(std::chrono::steady_clock::now(),
								  std::chrono::seconds(1), print,
								  "I run immediately and every second");

	timer_pool.push_task_once(std::chrono::steady_clock::now() +
								  std::chrono::seconds(3),
							  print, "I run after 3 seconds");

	timer_pool.push_task_periodic(
		std::chrono::steady_clock::now() + std::chrono::seconds(1),
		std::chrono::milliseconds(500), print,
		"I run after 1 second and every 500 milliseconds");

	std::this_thread::sleep_for(3050ms);
	timer_pool.push_task_once(std::chrono::steady_clock::now() + 100ms, print,
							  "I run after 3.15 seconds");

	std::this_thread::sleep_for(9999999s);
}