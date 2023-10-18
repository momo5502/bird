#pragma once

#include <queue>
#include <chrono>
#include <future>
#include <string>
#include <optional>

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

				this->callback_ = std::move(obj.callback_);
				this->token_ = std::move(obj.token_);

				obj.callback_ = {};
				obj.token_ = {};
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
				if (this->callback_)
				{
					this->callback_({});
					this->callback_ = {};
				}
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

	using query_queue = std::queue<query>;

	using headers = std::unordered_map<std::string, std::string>;

	std::optional<std::string> post_data(const std::string& url, const std::string& post_body,
	                                     const headers& headers = {}, const std::function<void(size_t)>& callback = {},
	                                     uint32_t retries = 2);
	std::optional<std::string> get_data(const std::string& url, const headers& headers = {},
	                                    const std::function<void(size_t)>& callback = {}, uint32_t retries = 2);

	class downloader
	{
	public:
		downloader();
		~downloader();

		downloader(const downloader&) = delete;
		downloader& operator=(const downloader&) = delete;

		downloader(downloader&&) = delete;
		downloader& operator=(downloader&&) = delete;

		std::future<result> download(url_string url, std::stop_token token = {});
		void download(url_string url, result_function function, std::stop_token token = {});

		void work(std::chrono::milliseconds timeout);

	private:
		concurrency::container<query_queue> queue_{};
		std::condition_variable cv_{};

		class worker;
		std::unique_ptr<worker> worker_;
	};
}
