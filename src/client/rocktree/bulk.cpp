#include "../std_include.hpp"

#include "bulk.hpp"
#include "node.hpp"
#include "rocktree.hpp"

#include "rocktree_proto.hpp"

namespace
{
	oriented_bounding_box unpack_obb(const std::string& packed, const glm::vec3& head_node_center,
	                                 const double meters_per_texel)
	{
		assert(packed.size() == 15);

		const auto* data = reinterpret_cast<const uint8_t*>(packed.data());

		oriented_bounding_box obb{};

		const auto* center_data = reinterpret_cast<const int16_t*>(data);

		obb.center[0] = static_cast<double>(center_data[0]) * meters_per_texel + head_node_center[0];
		obb.center[1] = static_cast<double>(center_data[1]) * meters_per_texel + head_node_center[1];
		obb.center[2] = static_cast<double>(center_data[2]) * meters_per_texel + head_node_center[2];

		obb.extents[0] = static_cast<double>(data[6]) * meters_per_texel;
		obb.extents[1] = static_cast<double>(data[7]) * meters_per_texel;
		obb.extents[2] = static_cast<double>(data[8]) * meters_per_texel;

		glm::dvec3 euler{};
		const auto* euler_data = reinterpret_cast<const int16_t*>(data + 9);

		euler[0] = static_cast<double>(euler_data[0]) * glm::pi<double>() / 32768.0;
		euler[1] = static_cast<double>(euler_data[1]) * glm::pi<double>() / 65536.0;
		euler[2] = static_cast<double>(euler_data[2]) * glm::pi<double>() / 32768.0;

		const double c0 = cos(euler[0]);
		const double s0 = sin(euler[0]);
		const double c1 = cos(euler[1]);
		const double s1 = sin(euler[1]);
		const double c2 = cos(euler[2]);
		const double s2 = sin(euler[2]);

		obb.orientation[0][0] = c0 * c2 - c1 * s0 * s2;
		obb.orientation[0][1] = c1 * c0 * s2 + c2 * s0;
		obb.orientation[0][2] = s2 * s1;
		obb.orientation[1][0] = -c0 * s2 - c2 * c1 * s0;
		obb.orientation[1][1] = c0 * c1 * c2 - s0 * s2;
		obb.orientation[1][2] = c2 * s1;
		obb.orientation[2][0] = s1 * s0;
		obb.orientation[2][1] = -c0 * s1;
		obb.orientation[2][2] = c1;

		return obb;
	}

	struct node_data_path_and_flags
	{
		std::string path{};
		uint32_t flags{};
		int level{};
	};

	node_data_path_and_flags unpack_path_and_flags(const NodeMetadata& node_meta)
	{
		node_data_path_and_flags result{};
		auto path_id = node_meta.path_and_flags();

		result.level = static_cast<int>(1 + (path_id & 3));
		path_id >>= 2;
		for (int i = 0; i < result.level; i++)
		{
			result.path.push_back(static_cast<char>('0' + (path_id & 7)));
			path_id >>= 3;
		}
		result.flags = path_id;

		return result;
	}
}


bulk::bulk(rocktree& rocktree, const generic_object& parent, static_bulk_data&& sdata)
	: rocktree_object(rocktree, &parent)
	  , sdata_(std::move(sdata))
{
}

const octant_identifier<>& bulk::get_path() const
{
	return this->sdata_.path;
}

std::string bulk::get_filename() const
{
	return "pb=!1m2!1s" + this->get_path().to_string() + "!2u" + std::to_string(this->sdata_.epoch);
}

std::string bulk::get_url() const
{
	return "BulkMetadata/" + this->get_filename();
}

std::filesystem::path bulk::get_filepath() const
{
	return "BulkMetadata" / octant_path_to_directory(this->get_path().to_string()) / this->get_filename();
}

void bulk::populate(const std::optional<std::string>& data)
{
	BulkMetadata bulk_meta{};
	if (!data || !bulk_meta.ParseFromString(*data))
	{
		throw std::runtime_error{"Failed to fetch bulk"};
	}

	this->head_node_center[0] = bulk_meta.head_node_center(0);
	this->head_node_center[1] = bulk_meta.head_node_center(1);
	this->head_node_center[2] = bulk_meta.head_node_center(2);

	for (const auto& node_meta : bulk_meta.node_metadata())
	{
		const auto aux = unpack_path_and_flags(node_meta);

		const bool has_data = !(aux.flags & NodeMetadata_Flags_NODATA);
		const bool is_leaf = (aux.flags & NodeMetadata_Flags_LEAF);
		const bool use_imagery_epoch = (aux.flags & NodeMetadata_Flags_USE_IMAGERY_EPOCH);
		const bool has_bulk = aux.path.size() == 4 && !is_leaf;
		const bool has_nodes = has_data || !is_leaf;

		if (has_bulk)
		{
			const auto epoch = node_meta.has_bulk_metadata_epoch()
				                   ? node_meta.bulk_metadata_epoch()
				                   : bulk_meta.head_node_key().epoch();

			this->bulks[aux.path] = this->allocate_object<bulk>(
				this->get_rocktree(), *this, static_bulk_data{epoch, this->get_path() + aux.path});
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


		auto n = this->get_rocktree().allocate_node(*this,
		                                            static_node_data{
			                                            node_meta.has_epoch() ? node_meta.epoch() : this->sdata_.epoch,
			                                            this->get_path() + aux.path, texture_format,
			                                            std::move(imagery_epoch), is_leaf
		                                            });

		n->can_have_data = has_data;
		n->meters_per_texel = node_meta.has_meters_per_texel()
			                      ? node_meta.meters_per_texel()
			                      : bulk_meta.meters_per_texel(aux.level - 1);
		n->obb = unpack_obb(node_meta.oriented_bounding_box(), this->head_node_center,
		                    n->meters_per_texel);

		this->nodes[aux.path] = std::move(n);
	}
}

void bulk::clear()
{
	for (const auto& node : this->nodes | std::views::values)
	{
		node->unlink_from(*this);
	}

	for (const auto& bulk : this->bulks | std::views::values)
	{
		bulk->unlink_from(*this);
	}

	this->nodes.clear();
	this->bulks.clear();
}
