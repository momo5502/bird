#include "std_include.hpp"

#include "rocktree.hpp"

#include <rocktree.pb.h>

#include <utils/io.hpp>
#include <utils/http.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <crn.h>
#include <ranges>

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

	std::string build_cache_url(const std::string_view& planet, const std::string_view& path)
	{
		static constexpr char base_url[] = R"(C:\Users\mauri\source\repos\bird\build\vs2022\cache/)";

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


	std::optional<std::string> fetch_google_data(const std::string_view& planet, const std::string_view& path,
	                                             bool use_cache = true)
	{
		const auto cache_url = build_cache_url(planet, path);
		std::string data{};
		if (use_cache && utils::io::read_file(cache_url, &data))
		{
			return data;
		}

		const auto url = build_google_url(planet, path);
		auto fetched_data = fetch_data(url);

		if (use_cache && fetched_data)
		{
			utils::io::write_file(cache_url, *fetched_data);
		}

		return fetched_data;
	}
}

rocktree_object::rocktree_object(rocktree& rocktree)
	: rocktree_(&rocktree)
{
}

const std::string& rocktree_object::get_planet() const
{
	return this->get_rocktree().get_planet();
}

void rocktree_object::fetch()
{
	if (is_ready())
	{
		return;
	}

	state expected{state::fresh};
	if (!this->state_.compare_exchange_strong(expected, state::fetching))
	{
		return;
	}

	this->get_rocktree().task_manager_.schedule([this]
	{
		try
		{
			this->populate();
			this->state_ = state::ready;
		}
		catch (...)
		{
			this->state_ = state::failed;
		}
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

// unpackVarInt unpacks variable length integer from proto (like coded_stream.h)
int unpackVarInt(const std::string& packed, int* index)
{
	auto data = (uint8_t*)packed.data();
	auto size = packed.size();
	int c = 0, d = 1, e;
	do
	{
		assert(*index < size);
		e = data[(*index)++];
		c += (e & 0x7F) * d;
		d <<= 7;
	}
	while (e & 0x80);
	return c;
}

// vertex is a packed struct for an 8-byte-per-vertex array
#pragma pack(push, 1)
struct vertex_t
{
	uint8_t x, y, z; // position
	uint8_t w; // octant mask
	uint16_t u, v; // texture coordinates
};
#pragma pack(pop)
static_assert((sizeof(vertex_t) == 8), "vertex_t size must be 8");

// unpackVertices unpacks vertices XYZ to new 8-byte-per-vertex array
std::vector<uint8_t> unpackVertices(const std::string& packed)
{
	auto data = (uint8_t*)packed.data();
	auto count = packed.size() / 3;
	auto vertices = std::vector<uint8_t>(count * sizeof(vertex_t));
	auto vtx = (vertex_t*)vertices.data();
	uint8_t x = 0, y = 0, z = 0; // 8 bit for % 0x100
	for (auto i = 0; i < count; i++)
	{
		vtx[i].x = x += data[count * 0 + i];
		vtx[i].y = y += data[count * 1 + i];
		vtx[i].z = z += data[count * 2 + i];
	}
	return vertices;
}

// unpackTexCoords unpacks texture coordinates UV to 8-byte-per-vertex-array
void unpackTexCoords(const std::string& packed, uint8_t* vertices, size_t vertices_len, glm::vec2& uv_offset,
                     glm::vec2& uv_scale)
{
	auto data = (uint8_t*)packed.data();
	auto count = vertices_len / sizeof(vertex_t);
	assert(count * 4 == (packed.size() - 4) && packed.size() >= 4);
	auto u_mod = 1 + *(uint16_t*)(data + 0);
	auto v_mod = 1 + *(uint16_t*)(data + 2);
	data += 4;
	auto vtx = (vertex_t*)vertices;
	auto u = 0, v = 0;
	for (size_t i = 0; i < count; i++)
	{
		vtx[i].u = u = (u + data[count * 0 + i] + (data[count * 2 + i] << 8)) % u_mod;
		vtx[i].v = v = (v + data[count * 1 + i] + (data[count * 3 + i] << 8)) % v_mod;
	}

	uv_offset[0] = 0.5;
	uv_offset[1] = 0.5;
	uv_scale[0] = 1.0 / u_mod;
	uv_scale[1] = 1.0 / v_mod;
}

// unpackIndices unpacks indices to triangle strip
std::vector<uint16_t> unpackIndices(const std::string& packed)
{
	auto offset = 0;

	auto triangle_strip_len = unpackVarInt(packed, &offset);
	auto triangle_strip = std::vector<uint16_t>(triangle_strip_len);
	auto num_non_degenerate_triangles = 0;
	for (int zeros = 0, a, b = 0, c = 0, i = 0; i < triangle_strip_len; i++)
	{
		int val = unpackVarInt(packed, &offset);
		triangle_strip[i] = (a = b, b = c, c = zeros - val);
		if (a != b && a != c && b != c) num_non_degenerate_triangles++;
		if (0 == val) zeros++;
	}

	return triangle_strip;
}

// unpackOctantMaskAndOctantCountsAndLayerBounds unpacks the octant mask for vertices (W) and layer bounds and octant counts
void unpackOctantMaskAndOctantCountsAndLayerBounds(const std::string& packed, const uint16_t* indices, size_t indices_len,
                                                   uint8_t* vertices, size_t vertices_len, int layer_bounds[10])
{
	// todo: octant counts
	auto offset = 0;
	auto len = unpackVarInt(packed, &offset);
	auto idx_i = 0;
	auto k = 0;
	auto m = 0;

	for (auto i = 0; i < len; i++)
	{
		if (0 == i % 8)
		{
			assert(m < 10);
			layer_bounds[m++] = k;
		}
		auto v = unpackVarInt(packed, &offset);
		for (auto j = 0; j < v; j++)
		{
			auto idx = indices[idx_i++];
			assert(0 <= idx && idx < indices_len);
			auto vtx_i = idx;
			assert(0 <= vtx_i && vtx_i < vertices_len / sizeof(vertex_t));
			((vertex_t*)vertices)[vtx_i].w = i & 7;
		}
		k += v;
	}

	for (; 10 > m; m++) layer_bounds[m] = k;
}


void node::populate()
{
	const auto texture_format = std::to_string(this->format_ == texture_format::rgb
		                                           ? Texture_Format_JPG
		                                           : Texture_Format_DXT1);

	const auto url_path = this->imagery_epoch_
		                      ? ("NodeData/pb=!1m2!1s" + this->path_ + "!2u" +
			                      std::to_string(this->epoch_) + "!2e" + texture_format + "!3u" +
			                      std::to_string(*this->imagery_epoch_) + "!4b0")
		                      : ("NodeData/pb=!1m2!1s" + this->path_ + "!2u" + std::to_string(
			                      this->epoch_) + "!2e" + texture_format + "!4b0");

	const auto data = fetch_google_data(this->get_planet(), url_path);

	NodeData node_data{};
	if (!data || !node_data.ParseFromString(*data))
	{
		throw std::runtime_error{""};
	}

	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			this->matrix_globe_from_mesh[i][j] = node_data.matrix_globe_from_mesh(4 * i + j);
		}
	}

	for (const auto& mesh : node_data.meshes())
	{
		::mesh m;

		m.indices = unpackIndices(mesh.indices());
		m.vertices = unpackVertices(mesh.vertices());

		unpackTexCoords(mesh.texture_coordinates(), m.vertices.data(), m.vertices.size(), m.uv_offset, m.uv_scale);
		if (mesh.uv_offset_and_scale_size() == 4)
		{
			m.uv_offset[0] = mesh.uv_offset_and_scale(0);
			m.uv_offset[1] = mesh.uv_offset_and_scale(1);
			m.uv_scale[0] = mesh.uv_offset_and_scale(2);
			m.uv_scale[1] = mesh.uv_offset_and_scale(3);
		}
		else
		{
			//m.uv_offset[1] -= 1 / m.uv_scale[1];
			//m.uv_scale[1] *= -1;
		}

		int layer_bounds[10];
		unpackOctantMaskAndOctantCountsAndLayerBounds(mesh.layer_and_octant_counts(), m.indices.data(),
		                                              m.indices.size(), m.vertices.data(), m.vertices.size(),
		                                              layer_bounds);
		assert(0 <= layer_bounds[3] && layer_bounds[3] <= m.indices.size());
		//m.indices_len = layer_bounds[3]; // enable
		m.indices.resize(layer_bounds[3]);

		auto textures = mesh.texture();
		assert(textures.size() == 1);
		auto texture = textures[0];
		assert(texture.data().size() == 1);
		auto tex = texture.data()[0];

		// maybe: keep compressed in memory?
		if (texture.format() == Texture_Format_JPG)
		{
			auto data = reinterpret_cast<uint8_t*>(tex.data());
			int width, height, comp;
			unsigned char* pixels = stbi_load_from_memory(&data[0], static_cast<int>(tex.size()), &width, &height, &comp, 0);
			assert(pixels != NULL);
			assert(width == texture.width() && height == texture.height() && comp == 3);
			m.texture = std::vector<uint8_t>(pixels, pixels + width * height * comp);
			stbi_image_free(pixels);
			m.format = texture_format::rgb;
		}
		else if (texture.format() == Texture_Format_CRN_DXT1)
		{
			auto src_size = tex.size();
			auto src = reinterpret_cast<uint8_t*>(tex.data());
			auto dst_size = crn_get_decompressed_size(src, static_cast<uint32_t>(src_size), 0);
			assert(dst_size == ((texture.width() + 3) / 4) * ((texture.height() + 3) / 4) * 8);
			m.texture = std::vector<uint8_t>(dst_size);
			crn_decompress(src,static_cast<uint32_t>(src_size), m.texture.data(), dst_size, 0);
			m.format = texture_format::dxt1;
		}
		else
		{
			fprintf(stderr, "unsupported texture format: %d\n", texture.format());
			abort();
		}

		m.texture_width = texture.width();
		m.texture_height = texture.height();

		m.buffered = false;
		this->meshes.emplace_back(std::move(m));
	}
}

bulk::bulk(rocktree& rocktree, const uint32_t epoch, std::string path)
	: rocktree_object(rocktree)
	  , epoch_(epoch)
	  , path_(std::move(path))
{
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

	for (const auto& bulk : this->bulks | std::views::values)
	{
		if (!bulk->can_be_removed())
		{
			return false;
		}
	}

	for (const auto& node : this->nodes | std::views::values)
	{
		if (!node->can_be_removed())
		{
			return false;
		}
	}

	return true;
}

const std::string& bulk::get_path() const
{
	return this->path_;
}

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

void bulk::populate()
{
	const auto data = fetch_google_data(this->get_planet(),
	                                    "BulkMetadata/pb=!1m2!1s" + this->path_ + "!2u" + std::to_string(this->epoch_));

	BulkMetadata bulk_meta{};
	if (!data || !bulk_meta.ParseFromString(*data))
	{
		throw std::runtime_error{""};
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
			auto epoch = node_meta.has_bulk_metadata_epoch()
				             ? node_meta.bulk_metadata_epoch()
				             : bulk_meta.head_node_key().epoch();

			this->bulks[aux.path] = std::make_unique<bulk>(this->get_rocktree(), epoch, this->path_ + aux.path);
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

		auto n = std::make_unique<node>(this->get_rocktree(), node_meta.has_epoch() ? node_meta.epoch() : this->epoch_,
		                                this->path_ + aux.path, texture_format,
		                                std::move(imagery_epoch));

		n->can_have_data = has_data;
		n->meters_per_texel = node_meta.has_meters_per_texel()
			                      ? node_meta.meters_per_texel()
			                      : bulk_meta.meters_per_texel(static_cast<int>(aux.level) - 1);
		n->obb = unpack_obb(node_meta.oriented_bounding_box(), this->head_node_center, n->meters_per_texel);

		this->nodes[aux.path] = std::move(n);
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
	const auto data = fetch_google_data(this->get_planet(), "PlanetoidMetadata", false);

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
