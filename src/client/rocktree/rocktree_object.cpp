#include "rocktree_object.hpp"

#include "rocktree.hpp"

#include <utils/io.hpp>

namespace
{
	XXH32_hash_t calculate_hash(const void* data, const size_t size)
	{
		return XXH32(data, size, 0x12345678);
	}

	XXH32_hash_t calculate_hash(const std::string_view& data)
	{
		return calculate_hash(data.data(), data.size());
	}

	bool write_cache_file(const std::filesystem::path& file, std::string data)
	{
		const auto hash = calculate_hash(data);
		data.append(reinterpret_cast<const char*>(&hash), sizeof(hash));

		return utils::io::write_file(file, data);
	}

	std::optional<std::string> read_cache_file(const std::filesystem::path& file)
	{
		std::string data{};
		if (!utils::io::read_file(file, &data))
		{
			return {};
		}

		XXH32_hash_t stored_hash{};
		constexpr auto hash_size = sizeof(stored_hash);
		if (data.size() < hash_size)
		{
			return {};
		}

		memcpy(&stored_hash, data.data() + data.size() - hash_size, hash_size);

		const auto* start = data.data();
		const auto size = data.size() - hash_size;


		const auto calculated_hash = calculate_hash(start, size);
		if (stored_hash != calculated_hash)
		{
			return {};
		}

		data.resize(size);
		return data;
	}

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

	std::filesystem::path build_cache_url(const std::string_view& planet, const std::filesystem::path& path)
	{
		return std::filesystem::temp_directory_path() / "bird" / (planet / path);
	}

	void fetch_google_data(task_manager& manager, utils::http::downloader& downloader, const std::string_view& planet,
	                       const std::string_view& path,
	                       const std::filesystem::path& file_path,
	                       utils::http::result_function callback, utils::thread::stop_token token, const bool prefer_cache,
	                       const bool high_priority)
	{
		if (token.stop_requested())
		{
			callback({});
			return;
		}

		auto cache_url = build_cache_url(planet, file_path);
		if (prefer_cache)
		{
			auto data = read_cache_file(cache_url);
			if (data)
			{
				callback(std::move(data));
				return;
			}
		}

		auto dispatcher = [cache_url = std::move(cache_url), cb = std::move(callback), &manager](
			std::optional<std::string> result)
		{
			if (result)
			{
				cb(result);

				manager.schedule([c = std::move(cache_url), r = std::move(std::move(result))]
				{
					write_cache_file(c, std::move(*r));
				});
			}
			else
			{
				auto data = read_cache_file(cache_url);
				cb(std::move(data));
			}
		};

		const auto url = build_google_url(planet, path);
		downloader.download(
			url, [&manager, d = std::move(dispatcher)](utils::http::result result)
			{
				manager.schedule([r = std::move(result), dis = std::move(d)]
				{
					dis(std::move(r));
				}, 0, false);
			}, std::move(token), high_priority);
	}
}

rocktree_object::rocktree_object(rocktree& rocktree, const generic_object* parent)
	: generic_object(parent)
	  , rocktree_(&rocktree)
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
		catch (const std::exception& e)
		{
#ifdef NDEBUG
			(void)e;
#else
			puts(e.what());
#endif
			this->finish_fetching(false);
		}
	}, 1 + (this->is_high_priority() ? 0 : 1), true);
}

void rocktree_object::run_fetching()
{
	const auto file_path = this->get_filepath();
	const auto url_path = this->get_url();
	auto& rocktree = this->get_rocktree();

	fetch_google_data( //
		rocktree.task_manager_, rocktree.downloader_, rocktree.get_planet(), url_path,
		std::move(file_path),
		[this](const utils::http::result& res)
		{
			try
			{
				this->populate(res);
				this->finish_fetching(true);
			}
			catch (const std::exception& e)
			{
#ifdef NDEBUG
				(void)e;
#else
				puts(e.what());
#endif
				this->finish_fetching(false);
			}
		}, this->get_stop_token(), this->prefer_cache(), this->is_high_priority());
}

void rocktree_object::store_object(std::unique_ptr<rocktree_object> object) const
{
	this->get_rocktree().store_object(std::move(object));
}
