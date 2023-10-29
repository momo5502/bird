#include "std_include.hpp"

#include "rocktree.hpp"

#pragma warning(push)
#pragma warning(disable: 4100)
#pragma warning(disable: 4127)
#pragma warning(disable: 4244)
#pragma warning(disable: 6262)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <rocktree.pb.h>
#pragma warning(pop)

#include <utils/io.hpp>
#include <utils/http.hpp>

#include <crn.h>

#include <utils/timer.hpp>
#include <utils/finally.hpp>

using namespace geo_globetrotter_proto_rocktree;

namespace
{
	std::filesystem::path octant_path_to_directory(const std::string& path)
	{
		std::filesystem::path p = {};

		char path_chars[2] = {0, 0};

		for (const auto c : path)
		{
			path_chars[0] = c;
			p /= path_chars;
		}

		return p;
	}
}

node::node(rocktree& rocktree, const bulk& parent, const uint32_t epoch, std::string path, const texture_format format,
           std::optional<uint32_t> imagery_epoch)
	: rocktree_object(rocktree, &parent)
	  , epoch_(epoch)
	  , path_(std::move(path))
	  , format_(format)
	  , imagery_epoch_(std::move(imagery_epoch))
{
}

void node::buffer_meshes()
{
	if (this->buffer_meshes_internal())
	{
		this->mark_as_buffered();
	}
}

bool node::is_buffered() const
{
	return this->buffer_state_ == buffer_state::buffered;
}

bool node::is_buffering() const
{
	return this->buffer_state_ == buffer_state::buffering;
}

bool node::mark_for_buffering()
{
	auto expected = buffer_state::unbuffered;
	return this->buffer_state_.compare_exchange_strong(expected, buffer_state::buffering);
}

float node::draw(const shader_context& ctx, float current_time, const std::array<float, 8>& child_draw_time,
                  const std::array<int, 8>& octant_mask)
{
	if (!this->draw_time_)
	{
		this->draw_time_ = current_time;
	}

	const auto own_draw_time = *this->draw_time_;

	glUniform1f(ctx.current_time_loc, current_time);
	glUniform1f(ctx.own_draw_time_loc, own_draw_time);

	glUniform1iv(ctx.octant_mask_loc, 8, octant_mask.data());
	glUniform1fv(ctx.child_draw_times_loc, 8, child_draw_time.data());

	for (auto& mesh : this->meshes)
	{
		mesh.draw(ctx);
	}

	return own_draw_time;
}

void node::buffer_queue(std::queue<node*> nodes)
{
	std::queue<node*> nodes_to_notify{};

	while (!nodes.empty())
	{
		auto* node = nodes.front();
		nodes.pop();

		if (node && !node->is_being_deleted() && node->buffer_meshes_internal())
		{
			nodes_to_notify.push(node);
		}
	}

	glFinish();

	while (!nodes_to_notify.empty())
	{
		auto* node = nodes_to_notify.front();
		nodes_to_notify.pop();

		node->mark_as_buffered();
	}
}

int unpack_var_int(const std::string& packed, int* index)
{
	const auto* data = reinterpret_cast<const uint8_t*>(packed.data());
	const auto size = packed.size();

	int c = 0, d = 1, e{};
	do
	{
		if (*index >= static_cast<int>(size))
		{
			break;
		}

		e = data[(*index)++];
		c += (e & 0x7F) * d;
		d <<= 7;
	}
	while (e & 0x80);

	return c;
}

std::vector<vertex> unpack_vertices(const std::string& packed)
{
	const auto count = packed.size() / 3;
	const auto data = reinterpret_cast<const uint8_t*>(packed.data());

	uint8_t x = 0, y = 0, z = 0;

	auto vertices = std::vector<vertex>(count);

	for (size_t i = 0; i < count; i++)
	{
		x += data[count * 0 + i];
		y += data[count * 1 + i];
		z += data[count * 2 + i];

		vertices[i].x = x;
		vertices[i].y = y;
		vertices[i].z = z;
	}

	return vertices;
}

void unpack_tex_coords(const std::string& packed, std::vector<vertex>& vertices, glm::vec2& uv_offset,
                       glm::vec2& uv_scale)
{
	const auto count = vertices.size();
	auto data = reinterpret_cast<const uint8_t*>(packed.data());

	assert(count * 4 == (packed.size() - 4) && packed.size() >= 4);

	const auto u_mod = 1 + *reinterpret_cast<const uint16_t*>(data + 0);
	const auto v_mod = 1 + *reinterpret_cast<const uint16_t*>(data + 2);
	data += 4;

	auto u = 0, v = 0;
	for (size_t i = 0; i < count; i++)
	{
		u = (u + data[count * 0 + i] + (data[count * 2 + i] << 8)) % u_mod;
		v = (v + data[count * 1 + i] + (data[count * 3 + i] << 8)) % v_mod;

		vertices[i].u = static_cast<uint16_t>(u);
		vertices[i].v = static_cast<uint16_t>(v);
	}

	uv_offset[0] = 0.5;
	uv_offset[1] = 0.5;

	uv_scale[0] = static_cast<float>(1.0 / u_mod);
	uv_scale[1] = static_cast<float>(1.0 / v_mod);
}

// unpackIndices unpacks indices to triangle strip
std::vector<uint16_t> unpack_indices(const std::string& packed)
{
	auto offset = 0;

	const auto triangle_strip_len = unpack_var_int(packed, &offset);
	auto triangle_strip = std::vector<uint16_t>(triangle_strip_len);
	for (int zeros = 0, c = 0, i = 0; i < triangle_strip_len; ++i)
	{
		const int val = unpack_var_int(packed, &offset);

		c = zeros - val;

		triangle_strip[i] = static_cast<uint16_t>(c);
		if (0 == val) zeros++;
	}

	return triangle_strip;
}

void unpack_octant_mask_and_octant_counts_and_layer_bounds(const std::string& packed,
                                                           const std::vector<uint16_t>& indices,
                                                           std::vector<vertex>& vertices, int layer_bounds[10])
{
	// todo: octant counts
	auto offset = 0;
	const auto len = unpack_var_int(packed, &offset);
	auto idx_i = 0;
	auto k = 0;
	auto m = 0;

	for (auto i = 0; i < len; i++)
	{
		if (0 == i % 8)
		{
			if (m < 10)
			{
				layer_bounds[m++] = k;
			}
		}
		auto v = unpack_var_int(packed, &offset);
		for (auto j = 0; j < v; j++)
		{
			const auto idx = indices[idx_i++];
			if (idx < indices.size())
			{
				const auto vtx_i = idx;
				if (vtx_i < vertices.size())
				{
					vertices[vtx_i].w = i & 7;
				}
			}
		}
		k += v;
	}

	for (; 10 > m; m++) layer_bounds[m] = k;
}

std::string node::get_filename() const
{
	const auto texture_format = std::to_string(this->format_ == texture_format::rgb
		                                           ? Texture_Format_JPG
		                                           : Texture_Format_DXT1);

	if (this->imagery_epoch_)
	{
		return "pb=!1m2!1s" + this->path_ //
			+ "!2u" + std::to_string(this->epoch_) //
			+ "!2e" + texture_format //
			+ "!3u" + std::to_string(*this->imagery_epoch_) //
			+ "!4b0";
	}

	return "pb=!1m2!1s" + this->path_ //
		+ "!2u" + std::to_string(this->epoch_) //
		+ "!2e" + texture_format //
		+ "!4b0";
}

std::string node::get_url() const
{
	return "NodeData/" + this->get_filename();
}

std::filesystem::path node::get_filepath() const
{
	return "NodeData" / octant_path_to_directory(this->path_) / this->get_filename();
}

void node::populate(const std::optional<std::string>& data)
{
	NodeData node_data{};
	if (!data || !node_data.ParseFromString(*data))
	{
		throw std::runtime_error{"Failed to fetch node"};
	}

	if (node_data.matrix_globe_from_mesh_size() == 16)
	{
		for (int i = 0; i < 4; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				this->matrix_globe_from_mesh[i][j] = node_data.matrix_globe_from_mesh(4 * i + j);
			}
		}
	}

	this->meshes.reserve(static_cast<size_t>(node_data.meshes_size()));

	for (const auto& mesh : node_data.meshes())
	{
		mesh_data m{};

		m.indices = unpack_indices(mesh.indices());
		m.vertices = unpack_vertices(mesh.vertices());

		unpack_tex_coords(mesh.texture_coordinates(), m.vertices, m.uv_offset, m.uv_scale);
		if (mesh.uv_offset_and_scale_size() == 4)
		{
			m.uv_offset[0] = mesh.uv_offset_and_scale(0);
			m.uv_offset[1] = mesh.uv_offset_and_scale(1);
			m.uv_scale[0] = mesh.uv_offset_and_scale(2);
			m.uv_scale[1] = mesh.uv_offset_and_scale(3);
		}

		int layer_bounds[10];
		unpack_octant_mask_and_octant_counts_and_layer_bounds(mesh.layer_and_octant_counts(), m.indices, m.vertices,
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
			auto tex_data = reinterpret_cast<uint8_t*>(tex.data());
			int width, height, comp;
			unsigned char* pixels = stbi_load_from_memory(&tex_data[0], static_cast<int>(tex.size()), &width,
			                                              &height,
			                                              &comp, 0);
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
			crn_decompress(src, static_cast<uint32_t>(src_size), m.texture.data(), dst_size, 0);
			m.format = texture_format::dxt1;
		}
		else
		{
			fprintf(stderr, "unsupported texture format: %d\n", texture.format());
			abort();
		}

		m.texture_width = static_cast<int>(texture.width());
		m.texture_height = static_cast<int>(texture.height());

		this->meshes.emplace_back(std::move(m));
	}

	this->meshes.shrink_to_fit();
}

void node::clear()
{
	this->meshes.clear();
	this->buffer_state_ = buffer_state::unbuffered;
}

bool node::can_be_deleted() const
{
	return this->buffer_state_ != buffer_state::buffering;
}

bool node::buffer_meshes_internal()
{
	if (this->is_buffered())
	{
		return false;
	}

	for (auto& m : this->meshes)
	{
		m.buffer(this->get_rocktree().get_bufferer());
	}

	return true;
}

void node::mark_as_buffered()
{
	this->buffer_state_ = buffer_state::buffered;
}

bulk::bulk(rocktree& rocktree, const generic_object& parent, const uint32_t epoch, std::string path)
	: rocktree_object(rocktree, &parent)
	  , epoch_(epoch)
	  , path_(std::move(path))
{
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

std::string bulk::get_filename() const
{
	return "pb=!1m2!1s" + this->path_ + "!2u" + std::to_string(this->epoch_);
}

std::string bulk::get_url() const
{
	return "BulkMetadata/" + this->get_filename();
}

std::filesystem::path bulk::get_filepath() const
{
	return "BulkMetadata" / octant_path_to_directory(this->path_) / this->get_filename();
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
			auto epoch = node_meta.has_bulk_metadata_epoch()
				             ? node_meta.bulk_metadata_epoch()
				             : bulk_meta.head_node_key().epoch();

			this->bulks[aux.path] = this->allocate_object<bulk>(
				this->get_rocktree(), *this, epoch, this->path_ + aux.path);
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

		auto n = this->allocate_object<node>(this->get_rocktree(), *this,
		                                     node_meta.has_epoch() ? node_meta.epoch() : this->epoch_,
		                                     this->path_ + aux.path, texture_format,
		                                     std::move(imagery_epoch));

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

std::string planetoid::get_url() const
{
	return "PlanetoidMetadata";
}

std::filesystem::path planetoid::get_filepath() const
{
	return this->get_url();
}

void planetoid::populate(const std::optional<std::string>& data)
{
	PlanetoidMetadata planetoid{};
	if (!data || !planetoid.ParseFromString(*data))
	{
		throw std::runtime_error{"Failed to fetch planetoid"};
	}

	this->radius = planetoid.radius();
	this->root_bulk = this->allocate_object<bulk>(this->get_rocktree(), *this, planetoid.root_node_metadata().epoch());
}

void planetoid::clear()
{
	if (this->root_bulk)
	{
		this->root_bulk->unlink_from(*this);
		this->root_bulk = {};
	}
}

rocktree::rocktree(std::string planet)
	: planet_(std::move(planet))
{
	this->planetoid_ = std::make_unique<planetoid>(*this);
}

rocktree::~rocktree()
{
	this->downloader_.stop();
	this->task_manager_.stop();
}

void rocktree::cleanup_dangling_objects()
{
	this->objects_.access_with_lock([&](object_list& objects, std::unique_lock<std::mutex>& lock)
	{
		utils::timer timer{};

		for (auto i = objects.begin(); i != objects.end();)
		{
			if (timer.has_elapsed(1ms))
			{
				lock.unlock();
				std::this_thread::sleep_for(1ms);
				lock.lock();
				timer.update();
				return;
			}

			auto& object = **i;

			const auto is_unused = !object.has_parent();
			const auto is_final = object.is_in_final_state();

			if (is_unused && is_final)
			{
				i = objects.erase(i);
			}
			else
			{
				if (is_unused)
				{
					object.mark_for_deletion();
				}

				++i;
			}
		}
	});
}

size_t rocktree::get_tasks() const
{
	return this->task_manager_.get_tasks();
}

size_t rocktree::get_tasks(const size_t i) const
{
	return this->task_manager_.get_tasks(i);
}

size_t rocktree::get_downloads() const
{
	return this->downloader_.get_downloads();
}

void rocktree::store_object(std::unique_ptr<generic_object> object)
{
	this->objects_.access([&](object_list& list)
	{
		list.push_back(std::move(object));
	});
}
