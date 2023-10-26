#pragma once

class generic_object
{
protected:
	virtual void clear() = 0;
	virtual void populate() = 0;

	virtual bool can_be_deleted() const
	{
		return true;
	}

public:
	generic_object(const generic_object* parent)
		: parent_(parent)
	{
	}

	virtual ~generic_object() = default;

	generic_object(generic_object&&) = delete;
	generic_object(const generic_object&) = delete;
	generic_object& operator=(generic_object&&) = delete;
	generic_object& operator=(const generic_object&) = delete;

	void unlink_from(const generic_object& parent)
	{
		if (this->parent_ == &parent)
		{
			this->parent_ = nullptr;
		}
	}

	bool has_parent() const
	{
		return this->parent_ != nullptr;
	}

	bool is_being_deleted() const
	{
		const auto state = this->state_.load();
		return state == state::deleting;
	}

	bool is_in_final_state() const
	{
		const auto state = this->state_.load();
		return state == state::ready || state == state::failed || state == state::deleting;
	}

	bool is_fetching() const
	{
		return this->state_ == state::fetching;
	}

	bool can_be_used()
	{
		const auto state = this->state_.load();
		if (state == state::deleting || state == state::failed)
		{
			return false;
		}

		this->last_use_ = std::chrono::steady_clock::now();

		if (state == state::ready)
		{
			return true;
		}

		this->fetch();
		return false;
	}

	bool mark_for_deletion()
	{
		auto expected = state::fresh;
		if (!this->is_in_final_state() && !this->state_.compare_exchange_strong(expected, state::deleting))
		{
			this->source_.request_stop();
			return false;
		}

		this->state_ = state::deleting;
		this->source_.request_stop();
		return true;
	}

	bool try_perform_deletion()
	{
		if (this->state_ != state::deleting || !this->can_be_deleted())
		{
			return false;
		}

		this->clear();
		this->source_ = {};
		this->state_ = state::fresh;
		return true;
	}

	std::stop_token get_stop_token() const
	{
		return this->source_.get_token();
	}

	bool was_used_within(const std::chrono::milliseconds& duration) const
	{
		return (std::chrono::steady_clock::now() - this->last_use_.load()) < duration;
	}

protected:
	void finish_fetching(const bool success)
	{
		this->state_ = success ? state::ready : state::failed;
	}

private:
	enum class state
	{
		fresh,
		fetching,
		ready,
		deleting,
		failed,
	};

	const generic_object* parent_{nullptr};
	std::stop_source source_{};
	std::atomic<state> state_{state::fresh};
	std::atomic<std::chrono::steady_clock::time_point> last_use_{std::chrono::steady_clock::now()};

	void fetch()
	{
		auto expected = state::fresh;
		if (!this->state_.compare_exchange_strong(expected, state::fetching))
		{
			return;
		}

		try
		{
			this->populate();
		}
		catch (...)
		{
			this->finish_fetching(false);
		}
	}
};
