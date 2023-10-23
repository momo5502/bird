#pragma once
#include <utils/http.hpp>

inline int32_t get_available_threads()
{
	const auto total_threads = static_cast<int32_t>(std::thread::hardware_concurrency());
	const auto http_threads = static_cast<int32_t>(utils::http::downloader::get_default_thread_count());
	return total_threads - (2 + http_threads);
}

inline uint32_t get_task_manager_thread_count()
{
	return static_cast<uint32_t>(std::max(2, get_available_threads()));
}

class task_manager
{
public:
	using task = std::function<void()>;

	task_manager(size_t num_threads = get_task_manager_thread_count());
	~task_manager();

	task_manager(task_manager&&) = delete;
	task_manager(const task_manager&) = delete;
	task_manager& operator=(task_manager&&) = delete;
	task_manager& operator=(const task_manager&) = delete;

	void schedule(task t, bool is_high_priority = false);

	void stop();

private:
	bool stop_{false};

	std::mutex mutex_{};
	std::condition_variable condition_variable_{};

	std::deque<task> tasks_{};
	std::vector<std::thread> threads_{};

	void work();
};
