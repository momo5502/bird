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
	using query = std::pair<url_string, result_function>;
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

		std::future<result> download(url_string url);
		void download(url_string url, result_function function);

		void work(std::chrono::milliseconds timeout);

	private:
		concurrency::container<query_queue> queue_{};
		std::condition_variable cv_{};

		class worker;
		std::unique_ptr<worker> worker_;
	};
}
