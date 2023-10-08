#include "std_include.hpp"

#include "task_manager.hpp"

task_manager::task_manager(const size_t num_threads)
{
	this->threads_.resize(num_threads);

	for (auto& thread : this->threads_)
	{
		thread = std::thread([this]
		{
			this->work();
		});
	}
}

task_manager::~task_manager()
{
	{
		std::lock_guard<std::mutex> _{this->mutex_};
		this->stop_ = true;
		this->tasks_.clear();
	}

	this->condition_variable_.notify_all();

	for (auto& thread : this->threads_)
	{
		if (thread.joinable())
		{
			thread.join();
		}
	}
}

void task_manager::schedule(task t, const bool is_high_priority)
{
	std::lock_guard<std::mutex> _{this->mutex_};

	if (is_high_priority)
	{
		this->tasks_.push_front(std::move(t));
	}
	else
	{
		this->tasks_.push_back(std::move(t));
	}
}

void task_manager::work()
{
	auto should_wake_up = [this]
	{
		return this->stop_ || !this->tasks_.empty();
	};

	while (true)
	{
		std::unique_lock<std::mutex> lock{this->mutex_};

		if (!should_wake_up())
		{
			this->condition_variable_.wait_for(lock, 1s, should_wake_up);
		}

		if (this->stop_)
		{
			break;
		}

		if (this->tasks_.empty())
		{
			continue;
		}

		auto task = std::move(this->tasks_.front());
		this->tasks_.pop_front();

		lock.unlock();

		try
		{
			task();
		}
		catch (const std::exception& e)
		{
			puts(e.what());
		}
	}
}
