#pragma once
#include <utils/http.hpp>
#include <utils/priority_mutex.hpp>

inline int32_t get_available_threads()
{
	const auto total_threads = static_cast<int32_t>(std::thread::hardware_concurrency());
	constexpr auto used_threads = static_cast<int32_t>(utils::http::downloader::get_default_thread_count() + 3);
	return total_threads - used_threads;
}

inline uint32_t get_task_manager_thread_count()
{
	return static_cast<uint32_t>(std::max(3, get_available_threads()));
}

class task_manager
{
public:
	static constexpr size_t QUEUE_COUNT = 4;

	using task = std::function<void()>;

	task_manager(size_t num_threads = get_task_manager_thread_count());
	~task_manager();

	task_manager(task_manager&&) = delete;
	task_manager(const task_manager&) = delete;
	task_manager& operator=(task_manager&&) = delete;
	task_manager& operator=(const task_manager&) = delete;

	void schedule(task t, size_t priority = (QUEUE_COUNT - 1), bool is_high_priority_thread = false);

	void stop();

	size_t get_tasks() const;
	size_t get_tasks(size_t i) const;

private:
	bool stop_{false};

	utils::priority_mutex mutex_{};
	std::condition_variable_any condition_variable_{};

	std::array<std::deque<task>, QUEUE_COUNT> queues_{};
	std::vector<std::thread> threads_{};

	void work();

	void schedule(std::deque<task>& q, task t, bool is_high_priority_thread);
};
