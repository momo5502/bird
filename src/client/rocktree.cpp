#include "std_include.hpp"

#include "rocktree.hpp"

#include <rocktree.pb.h>

#include <utils/http.hpp>

using namespace geo_globetrotter_proto_rocktree;

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

	std::optional<std::string> fetch_google_data(const std::string_view& planet, const std::string_view& path)
	{
		const auto url = build_google_url(planet, path);
		return fetch_data(url);
	}
}

rocktree_object::rocktree_object(rocktree& rocktree)
	: rocktree_(&rocktree)
{
}

rocktree_object::~rocktree_object()
{
	while (this->is_fetching())
	{
		std::this_thread::sleep_for(1ms);
	}
}

const std::string& rocktree_object::get_planet() const
{
	return this->get_rocktree().get_planet();
}

void rocktree_object::fetch()
{
	state expected{state::fresh};
	if (!this->state_.compare_exchange_strong(expected, state::fetching))
	{
		return;
	}

	this->get_rocktree().task_manager_.schedule([this]
	{
		this->populate();
		this->state_ = state::ready;
	}, this->is_high_priority());
}

node::node(rocktree& rocktree, const uint32_t epoch, std::string path, const texture_format format,
           std::optional<uint32_t> imagery_epoch)
	: rocktree_object(rocktree)
	  , epoch_(epoch)
	  , path_(std::move(path))
	  , format_(format)
	  , imagery_epoch_(std::move(imagery_epoch))
{
}

node::~node()
{
	while (!this->node::can_be_removed())
	{
		std::this_thread::sleep_for(1ms);
	}
}

void node::populate()
{
	const auto texture_format = std::to_string(this->format_ == texture_format::rgb
		                                           ? Texture_Format_JPG
		                                           : Texture_Format_DXT1);

	const auto data = fetch_google_data(this->get_planet(),
	                                    this->imagery_epoch_
		                                    ? ("NodeData/pb=!1m2!1s" + this->path_ + "!2u" +
			                                    std::to_string(this->epoch_) + "!2e" + texture_format + "!3u" +
			                                    std::to_string(*this->imagery_epoch_) + "!4b0")
		                                    : ("NodeData/pb=!1m2!1s" + this->path_ + "!2u" + std::to_string(
			                                    this->epoch_) + "!2e" + texture_format + "!4b0"));

	BulkMetadata bulk_meta{};
	if (!data || !bulk_meta.ParseFromString(*data))
	{
		return;
	}
}

bulk::bulk(rocktree& rocktree, const uint32_t epoch, std::string path)
	: rocktree_object(rocktree)
	  , epoch_(epoch)
	  , path_(std::move(path))
{
}

bulk::~bulk()
{
	while (!this->bulk::can_be_removed())
	{
		std::this_thread::sleep_for(1ms);
	}
}

struct node_data_path_and_flags
{
	std::string path{};
	uint32_t flags{};
	uint32_t level{};
};

node_data_path_and_flags unpack_path_and_flags(const NodeMetadata& node_meta)
{
	node_data_path_and_flags result{};
	auto path_id = node_meta.path_and_flags();

	result.level = 1 + (path_id & 3);
	path_id >>= 2;
	for (int i = 0; i < result.level; i++)
	{
		result.path.push_back(static_cast<char>('0' + (path_id & 7)));
		path_id >>= 3;
	}
	result.flags = path_id;

	return result;
}

bool bulk::can_be_removed() const
{
	if (!rocktree_object::can_be_removed())
	{
		return false;
	}

	if (!this->is_ready())
	{
		return true;
	}

	for (const auto& bulk : this->bulks)
	{
		if (!bulk.second->can_be_removed())
		{
			return false;
		}
	}

	for (const auto& node : this->nodes)
	{
		if (!node.second->can_be_removed())
		{
			return false;
		}
	}

	return true;
}

oriented_bounding_box unpack_obb(const std::string& packed, const glm::vec3& head_node_center,
                                 const double meters_per_texel)
{
	assert(packed.size() == 15);

	const auto* data = reinterpret_cast<const uint8_t*>(packed.data());

	oriented_bounding_box obb{};

	const auto* center_data = reinterpret_cast<const int16_t*>(data);

	obb.center[0] = static_cast<float>(center_data[0]) * meters_per_texel + head_node_center[0];
	obb.center[1] = static_cast<float>(center_data[1]) * meters_per_texel + head_node_center[1];
	obb.center[2] = static_cast<float>(center_data[2]) * meters_per_texel + head_node_center[2];

	obb.extents[0] = static_cast<float>(data[6]) * meters_per_texel;
	obb.extents[1] = static_cast<float>(data[7]) * meters_per_texel;
	obb.extents[2] = static_cast<float>(data[8]) * meters_per_texel;

	glm::dvec3 euler{};
	const auto* euler_data = reinterpret_cast<const int16_t*>(data + 9);

	euler[0] = static_cast<float>(euler_data[0]) * glm::pi<double>() / 32768.0;
	euler[1] = static_cast<float>(euler_data[1]) * glm::pi<double>() / 65536.0;
	euler[2] = static_cast<float>(euler_data[2]) * glm::pi<double>() / 32768.0;

	const double c0 = cos(euler[0]);
	const double s0 = sin(euler[0]);
	const double c1 = cos(euler[1]);
	const double s1 = sin(euler[1]);
	const double c2 = cos(euler[2]);
	const double s2 = sin(euler[2]);

	obb.orientation[0][0] = c0 * c2 - c1 * s0 * s2;
	obb.orientation[1][1] = c1 * c0 * s2 + c2 * s0;
	obb.orientation[2][2] = s2 * s1;
	obb.orientation[1][0] = -c0 * s2 - c2 * c1 * s0;
	obb.orientation[1][1] = c0 * c1 * c2 - s0 * s2;
	obb.orientation[1][2] = c2 * s1;
	obb.orientation[2][0] = s1 * s0;
	obb.orientation[2][1] = -c0 * s1;
	obb.orientation[2][2] = c1;

	return obb;
}

void bulk::populate()
{
	const auto data = fetch_google_data(this->get_planet(),
	                                    "BulkMetadata/pb=!1m2!1s" + this->path_ + "!2u" + std::to_string(this->epoch_));

	BulkMetadata bulk_meta{};
	if (!data || !bulk_meta.ParseFromString(*data))
	{
		return;
	}

	this->head_node_center[0] = bulk_meta.head_node_center(0);
	this->head_node_center[1] = bulk_meta.head_node_center(1);
	this->head_node_center[2] = bulk_meta.head_node_center(2);

	for (const auto& node_meta : bulk_meta.node_metadata())
	{
		const auto aux = unpack_path_and_flags(node_meta);
		const auto has_data = !(aux.flags & NodeMetadata_Flags_NODATA);
		const auto is_leaf = (aux.flags & NodeMetadata_Flags_LEAF);
		const auto use_imagery_epoch = (aux.flags & NodeMetadata_Flags_USE_IMAGERY_EPOCH);
		const auto has_bulk = aux.path.size() == 4 && !is_leaf;
		const auto has_nodes = has_data || !is_leaf;

		if (has_bulk)
		{
			auto epoch = node_meta.has_bulk_metadata_epoch()
				             ? node_meta.bulk_metadata_epoch()
				             : bulk_meta.head_node_key().epoch();

			this->bulks[aux.path] = std::make_unique<bulk>(this->get_rocktree(), epoch, aux.path);
		}

		if (!has_nodes || !node_meta.has_oriented_bounding_box())
		{
			continue;
		}

		const auto available_formats = node_meta.has_available_texture_formats()
			                               ? node_meta.available_texture_formats()
			                               : bulk_meta.default_available_texture_formats();

		auto texture_format = texture_format::dxt1;
		if (available_formats & (1 << (Texture_Format_JPG - 1)))
		{
			texture_format = texture_format::rgb;
		}

		std::optional<uint32_t> imagery_epoch{};
		if (use_imagery_epoch)
		{
			imagery_epoch = node_meta.has_imagery_epoch()
				                ? node_meta.imagery_epoch()
				                : bulk_meta.default_imagery_epoch();
		}

		auto n = std::make_unique<node>(this->get_rocktree(), this->epoch_, this->path_, texture_format,
		                                std::move(imagery_epoch));

		n->can_have_data = has_data;
		n->meters_per_texel = node_meta.has_meters_per_texel()
			                      ? node_meta.meters_per_texel()
			                      : bulk_meta.meters_per_texel(static_cast<int>(aux.level) - 1);
		n->obb = unpack_obb(node_meta.oriented_bounding_box(), this->head_node_center, n->meters_per_texel);

		this->nodes[aux.path] = std::move(n);
	}
}

planetoid::~planetoid()
{
	while (!this->planetoid::can_be_removed())
	{
		std::this_thread::sleep_for(1ms);
	}
}

bool planetoid::can_be_removed() const
{
	if (!rocktree_object::can_be_removed())
	{
		return false;
	}

	if (!this->is_ready())
	{
		return true;
	}

	return !this->root_bulk || this->root_bulk->can_be_removed();
}

void planetoid::populate()
{
	const auto data = fetch_google_data(this->get_planet(), "PlanetoidMetadata");

	PlanetoidMetadata planetoid{};
	if (!data || !planetoid.ParseFromString(*data))
	{
		return;
	}

	this->radius = planetoid.radius();
	this->root_bulk = std::make_unique<bulk>(this->get_rocktree(), planetoid.root_node_metadata().epoch());
	this->root_bulk->fetch();
}

rocktree::rocktree(std::string planet)
	: planet_(std::move(planet))
{
	this->planetoid_ = std::make_unique<planetoid>(*this);
	this->planetoid_->fetch();
}
