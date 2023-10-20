#pragma once

inline uint32_t get_task_manager_thread_count()
{
	return std::max(15u, std::thread::hardware_concurrency());
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
