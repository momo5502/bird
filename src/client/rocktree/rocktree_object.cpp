#include "rocktree_object.hpp"

#include "../rocktree.hpp"

#include <utils/io.hpp>

namespace
{
	std::string build_google_url(const std::string_view& planet, const std::string_view& path)
	{
		static constexpr char base_url[] = "http://kh.google.com/rt/";

		std::string url{};
		// base_url nullterminator and slash cancel out
		url.reserve(sizeof(base_url) + planet.size() + path.size());

		url.append(base_url);
		url.append(planet);
		url.push_back('/');
		url.append(path);

		return url;
	}

	std::string build_cache_url(const std::string_view& planet, const std::string_view& path)
	{
		static constexpr char base_url[] = R"(cache/)";

		std::string url{};
		// base_url nullterminator and slash cancel out
		url.reserve(sizeof(base_url) + planet.size() + path.size());

		url.append(base_url);
		url.append(planet);
		url.push_back('/');
		url.append(path);

		return url;
	}

	std::optional<std::string> fetch_data(const std::string& url)
	{
		try
		{
			return utils::http::get_data(url);
		}
		catch (...)
		{
			return {};
		}
	}

	void fetch_google_data(task_manager& manager, utils::http::downloader& downloader, const std::string_view& planet,
	                       const std::string_view& path,
	                       utils::http::result_function callback, std::stop_token token, bool prefer_cache)
	{
		auto cache_url = build_cache_url(planet, path);
		std::string data{};
		if (prefer_cache && utils::io::read_file(cache_url, &data))
		{
			callback(std::move(data));
			return;
		}

		auto dispatcher = [cache_url = std::move(cache_url), cb = std::move(callback)](
			std::optional<std::string> result)
		{
			std::string data{};
			if (result)
			{
				utils::io::write_file(cache_url, *result);
				cb(std::move(result));
			}
			else if (utils::io::read_file(cache_url, &data))
			{
				cb(std::move(data));
			}
			else
			{
				cb({});
			}
		};

		const auto url = build_google_url(planet, path);
		downloader.download(
			url, [&manager, d = std::move(dispatcher)](utils::http::result result)
			{
				manager.schedule([r = std::move(result), dis = std::move(d)]
				{
					dis(std::move(r));
				});
			}, std::move(token));
	}
}

rocktree_object::rocktree_object(rocktree& rocktree)
	: rocktree_(&rocktree)
{
}

void rocktree_object::populate()
{
	this->get_rocktree().task_manager_.schedule([this]
	{
		try
		{
			this->run_fetching();
		}
		catch (...)
		{
			this->finish_fetching(false);
		}
	}, this->is_high_priority());
}

void rocktree_object::run_fetching()
{
	const auto url_path = this->get_url();
	auto& rocktree = this->get_rocktree();

	fetch_google_data( //
		rocktree.task_manager_, rocktree.downloader_, rocktree.get_planet(), url_path,
		[this](const utils::http::result& res)
		{
			try
			{
				this->populate(res);
				this->finish_fetching(true);
			}
			catch (...)
			{
				this->finish_fetching(false);
			}
		}, this->get_stop_token(), this->prefer_cache());
}

void rocktree_object::store_object(std::unique_ptr<rocktree_object> object) const
{
	this->get_rocktree().store_object(std::move(object));
}
