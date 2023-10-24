#pragma once

#include <deque>
#include <chrono>
#include <future>
#include <string>
#include <vector>
#include <thread>
#include <optional>
#include <unordered_map>

#include "concurrency.hpp"

namespace utils::http
{
	using url_string = std::string;
	using result = std::optional<std::string>;
	using result_function = std::function<void(result)>;

	class stoppable_result_callback
	{
	public:
		stoppable_result_callback() = default;

		stoppable_result_callback(result_function callback, std::stop_token token)
			: token_(std::move(token))
			  , callback_(std::move(callback))
		{
		}

		~stoppable_result_callback()
		{
			this->destroy();
		}

		stoppable_result_callback(const stoppable_result_callback&) = delete;
		stoppable_result_callback& operator=(const stoppable_result_callback&) = delete;

		stoppable_result_callback(stoppable_result_callback&& obj) noexcept
		{
			this->operator=(std::move(obj));
		}

		stoppable_result_callback& operator=(stoppable_result_callback&& obj) noexcept
		{
			if (this != &obj)
			{
				this->destroy();

				this->token_ = std::move(obj.token_);
				this->callback_ = std::move(obj.callback_);

				obj.token_ = {};
				obj.callback_ = {};
			}

			return *this;
		}

		void operator()(result r)
		{
			if (this->callback_)
			{
				if (this->is_stopped())
				{
					this->callback_({});
				}
				else
				{
					this->callback_(std::move(r));
				}

				this->callback_ = {};
			}
		}

		bool is_stopped() const
		{
			return !this->token_.stop_possible() || this->token_.stop_requested();
		}

	private:
		std::stop_token token_{};
		result_function callback_{};

		void destroy()
		{
			try
			{
				(*this)({});
			}
			catch (...)
			{
			}
		}
	};

	struct query
	{
		url_string url;
		stoppable_result_callback callback;
	};

	using query_queue = std::deque<query>;

	using headers = std::unordered_map<std::string, std::string>;

	std::optional<std::string> post_data(const std::string& url, const std::string& post_body,
	                                     const headers& headers = {}, const std::function<void(size_t)>& callback = {},
	                                     uint32_t retries = 2);
	std::optional<std::string> get_data(const std::string& url, const headers& headers = {},
	                                    const std::function<void(size_t)>& callback = {}, uint32_t retries = 2);

	class worker_thread
	{
	public:
		worker_thread(concurrency::container<query_queue>& queue, std::condition_variable& cv, size_t max_requests);
		~worker_thread();

		worker_thread(const worker_thread&) = delete;
		worker_thread& operator=(const worker_thread&) = delete;

		worker_thread(worker_thread&&) = delete;
		worker_thread& operator=(worker_thread&&) = delete;

		void wakeup() const;
		void stop();

		size_t get_downloads() const;

	private:
		concurrency::container<query_queue>* queue_{};
		std::condition_variable* cv_{};

		class worker;
		std::unique_ptr<worker> worker_;
		std::jthread thread_{};

		void work(const std::chrono::milliseconds& timeout) const;
	};

	class downloader
	{
	public:
		static constexpr size_t get_default_thread_count()
		{
			return 2;
		}

		static constexpr size_t get_max_simultaneous_downloads()
		{
			return 24;
		}

		downloader(size_t num_worker_threads = get_default_thread_count(),
		           size_t max_downloads = get_max_simultaneous_downloads());
		~downloader();

		downloader(const downloader&) = delete;
		downloader& operator=(const downloader&) = delete;

		downloader(downloader&&) = delete;
		downloader& operator=(downloader&&) = delete;

		std::future<result> download(url_string url, std::stop_token token = {}, bool high_priority = false);
		void download(url_string url, result_function function, std::stop_token token = {}, bool high_priority = false);

		void stop();

		size_t get_downloads() const;

	private:
		concurrency::container<query_queue> queue_{};
		std::condition_variable cv_{};
		std::vector<std::unique_ptr<worker_thread>> workers_{};

		void wakeup() const;
	};
}
