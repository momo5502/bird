#pragma once
#include <thread>
#include <memory>
#include <functional>
#include "nt.hpp"

namespace utils::thread
{
	class stop_source;

	class stop_token
	{
	public:
		friend stop_source;
		using shared_state = std::shared_ptr<std::atomic_bool>;

		bool stop_possible() const
		{
			return static_cast<bool>(this->state_);
		}

		bool stop_requested() const
		{
			return this->state_ && *this->state_;
		}

	private:
		shared_state state_{};
	};

	class stop_source
	{
	public:
		stop_source()
		{
			this->token_.state_ = std::make_unique<std::atomic_bool>(false);
		}

		void request_stop()
		{
			if (this->token_.state_)
			{
				*this->token_.state_ = true;
			}
		}

		stop_token get_token() const
		{
			return this->token_;
		}

	private:
		stop_token token_{};
	};

	class joinable_thread
	{
	public:
		joinable_thread() = default;

		joinable_thread(std::function<void(stop_token)> runner)
		{
			auto token = this->get_stop_token();
			this->thread_ = std::thread([r = std::move(runner), t = std::move(token)]
			{
				r(std::move(t));
			});
		}

		joinable_thread(const joinable_thread&) = delete;
		joinable_thread& operator=(const joinable_thread&) = delete;

		joinable_thread(joinable_thread&&) noexcept = default;
		joinable_thread& operator=(joinable_thread&&) noexcept = default;

		~joinable_thread()
		{
			this->request_stop();
			if (this->joinable())
			{
				this->join();
			}
		}

		std::thread::native_handle_type native_handle()
		{
			return this->thread_.native_handle();
		}

		stop_token get_stop_token() const
		{
			return this->source_.get_token();
		}

		void request_stop()
		{
			this->source_.request_stop();
		}

		bool joinable() const
		{
			return this->thread_.joinable();
		}

		void join()
		{
			this->thread_.join();
		}

	private:
		stop_source source_{};
		std::thread thread_{};
	};


#ifdef _WIN32
	bool set_name(HANDLE t, const std::string& name);
	bool set_name(DWORD id, const std::string& name);
#endif

	template <typename T>
	concept ThreadLike =
		requires(T t)
		{
			{ t.native_handle() } -> std::convertible_to<std::thread::native_handle_type>;
		};

	template <ThreadLike Thread>
	bool set_name(Thread& t, const std::string& name)
	{
		(void)t;
		(void)name;
#ifdef _WIN32
		return set_name(t.native_handle(), name);
#else
		return false;
#endif
	}

	bool set_name(const std::string& name);

	enum class priority
	{
		low,
		normal,
		high
	};

	bool set_priority(priority p);

	template <typename... Args>
	std::thread create_named_thread(const std::string& name, Args&&... args)
	{
		auto t = std::thread(std::forward<Args>(args)...);
		set_name(t, name);
		return t;
	}

	template <typename... Args>
	joinable_thread create_named_jthread(const std::string& name, Args&&... args)
	{
		auto t = joinable_thread(std::forward<Args>(args)...);
		set_name(t, name);
		return t;
	}

#ifdef _WIN32
	class handle
	{
	public:
		handle(const DWORD thread_id, const DWORD access = THREAD_ALL_ACCESS)
			: handle_(OpenThread(access, FALSE, thread_id))
		{
		}

		operator bool() const
		{
			return this->handle_;
		}

		operator HANDLE() const
		{
			return this->handle_;
		}

	private:
		nt::handle<> handle_{};
	};

	std::vector<DWORD> get_thread_ids();
	void for_each_thread(const std::function<void(HANDLE)>& callback, DWORD access = THREAD_ALL_ACCESS);

	void suspend_other_threads();
	void resume_other_threads();
#endif
}
