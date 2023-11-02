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
			utils::thread::set_priority(utils::thread::priority::low);
			this->work();
		});
	}
}

task_manager::~task_manager()
{
	this->stop();
}

void task_manager::schedule(std::deque<task>& q, task t, const bool is_high_priority_thread)
{
	if (is_high_priority_thread)
	{
		std::scoped_lock lock{this->mutex_.high_priority()};
		q.push_back(std::move(t));
	}
	else
	{
		std::scoped_lock lock{this->mutex_};
		q.push_back(std::move(t));
	}

	this->condition_variable_.notify_one();
}

void task_manager::schedule(task t, const size_t priority, const bool is_high_priority_thread)
{
	auto& q = this->queues_.at(std::min((this->queues_.size() - 1), priority));
	this->schedule(q, std::move(t), is_high_priority_thread);
}

void task_manager::stop()
{
	{
		std::lock_guard _{this->mutex_};
		this->stop_ = true;
		this->queues_ = {};
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
	size_t tasks = 0;
	for (const auto& q : queues_)
	{
		tasks += q.size();
	}

	return tasks;
}

size_t task_manager::get_tasks(const size_t i) const
{
	return this->queues_.at(i).size();
}

void task_manager::work()
{
	auto should_wake_up = [this]
	{
		if (this->stop_)
		{
			return true;
		}

		for (const auto& q : queues_)
		{
			if (!q.empty())
			{
				return true;
			}
		}

		return false;
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

		auto* q = &this->queues_.at(0);
		for (size_t i = 1; q->empty() && i < this->queues_.size(); ++i)
		{
			q = &this->queues_.at(i);
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

		std::this_thread::yield();
	}
}
