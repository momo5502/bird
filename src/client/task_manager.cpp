#include "std_include.hpp"

#include "task_manager.hpp"

#include <utils/thread.hpp>

task_manager::task_manager(const size_t num_threads)
{
	this->threads_.resize(num_threads);

	for (auto& thread : this->threads_)
	{
		thread = utils::thread::create_named_thread("Task Manager", [this]
		{
			this->work();
		});
	}
}

task_manager::~task_manager()
{
	{
		std::lock_guard _{this->mutex_};
		this->stop_ = true;
		this->tasks_low_.clear();
		this->tasks_high_.clear();
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

void task_manager::schedule(std::deque<task>& q, task t, const bool is_high_priority_task,
                            const bool is_high_priority_thread)
{
	if (is_high_priority_thread)
	{
		std::scoped_lock lock{this->mutex_.high_priority()};
		if (is_high_priority_task)
		{
			q.push_front(std::move(t));
		}
		else
		{
			q.push_back(std::move(t));
		}
	}
	else
	{
		std::scoped_lock lock{this->mutex_};
		if (is_high_priority_task)
		{
			q.push_front(std::move(t));
		}
		else
		{
			q.push_back(std::move(t));
		}
	}

	this->condition_variable_.notify_one();
}

void task_manager::schedule(task t, const uint32_t priority, const bool is_high_priority_thread)
{
	this->schedule(priority < 2 ? this->tasks_high_ : this->tasks_low_, std::move(t), priority % 2 == 0,
	               is_high_priority_thread);
}

void task_manager::stop()
{
	{
		std::lock_guard _{this->mutex_};
		this->stop_ = true;
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

size_t task_manager::get_tasks() const
{
	return this->tasks_low_.size() + this->tasks_high_.size();
}

void task_manager::work()
{
	auto should_wake_up = [this]
	{
		return this->stop_ || !this->tasks_high_.empty() || !this->tasks_low_.empty();
	};

	while (true)
	{
		std::unique_lock lock{this->mutex_};

		if (!should_wake_up())
		{
			this->condition_variable_.wait_for(lock, 1s, should_wake_up);
		}

		if (this->stop_)
		{
			break;
		}

		auto* q = &this->tasks_high_;
		if (q->empty())
		{
			q = &this->tasks_low_;
		}

		if (q->empty())
		{
			continue;
		}

		auto task = std::move(q->front());
		q->pop_front();

		lock.unlock();

		try
		{
			task();
		}
		catch (const std::exception& e)
		{
#ifdef NDEBUG
			(void)e;
#else
			puts(e.what());
#endif
		}
	}
}
