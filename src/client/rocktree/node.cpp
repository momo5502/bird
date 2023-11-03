#include "../std_include.hpp"

#include "node.hpp"
#include "bulk.hpp"
#include "rocktree.hpp"

#include "rocktree_proto.hpp"

#pragma warning(push)
#pragma warning(disable: 4100)
#pragma warning(disable: 4127)
#pragma warning(disable: 4244)
#pragma warning(disable: 6262)

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <crn.h>

#pragma warning(pop)

namespace
{
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
}

physics_node::physics_node(rocktree& rocktree, const std::vector<mesh>& meshes, const glm::dmat4& world_matrix)
	: rocktree_(&rocktree)
{
	if (meshes.empty())
	{
		return;
	}

	this->meshes_.reserve(meshes.size());

	bool has_indices{false};
	for (const auto& mesh : meshes)
	{
		const auto& mesh_data = mesh.get_mesh_data();
		if (mesh_data.indices.empty())
		{
			continue;
		}

		has_indices = true;

		physics_mesh p_mesh{};
		p_mesh.vertices_.reserve(mesh_data.vertices.size());

		for (const auto& vertex : mesh_data.vertices)
		{
			const glm::dvec4 local_position{
				static_cast<double>(vertex.x), //
				static_cast<double>(vertex.y), //
				static_cast<double>(vertex.z), //
				1.0,
			};

			const auto position = world_matrix * local_position;

			p_mesh.vertices_.emplace_back(physics_node::vertex{
				static_cast<float>(position.x), //
				static_cast<float>(position.y), //
				static_cast<float>(position.z), //
			});
		}

		// technically -2, but fuck it
		p_mesh.triangles_.reserve(mesh_data.indices.size());
		for (size_t i = 2; i < mesh_data.indices.size(); ++i)
		{
			auto& triangle = p_mesh.triangles_.emplace_back(physics_node::triangle{
				mesh_data.indices.at(i - 2), //
				mesh_data.indices.at(i - 1), //
				mesh_data.indices.at(i - 0), //
			});

			if (i & 1)
			{
				std::swap(triangle.x, triangle.y);
			}
		}

		this->meshes_.emplace_back(std::move(p_mesh));
	}

	if (!has_indices)
	{
		return;
	}

	rocktree.access_physics([this](reactphysics3d::PhysicsCommon& common, reactphysics3d::PhysicsWorld& world)
	{
		this->triangle_mesh_ = common.createTriangleMesh();

		for (auto& mesh : this->meshes_)
		{
			mesh.vertex_array_ = std::make_unique<reactphysics3d::TriangleVertexArray>(
				static_cast<uint32_t>(mesh.vertices_.size()), mesh.vertices_.data(),
				static_cast<uint32_t>(sizeof(physics_node::vertex)),
				static_cast<uint32_t>(mesh.triangles_.size()), mesh.triangles_.data(),
				static_cast<uint32_t>(sizeof(physics_node::triangle)),
				reactphysics3d::TriangleVertexArray::VertexDataType::VERTEX_FLOAT_TYPE,
				reactphysics3d::TriangleVertexArray::IndexDataType::INDEX_SHORT_TYPE);

			this->triangle_mesh_->addSubpart(mesh.vertex_array_.get());
		}

		this->concave_shape_ = common.createConcaveMeshShape(this->triangle_mesh_);

		this->body_ = world.createCollisionBody({});
		this->body_->addCollider(this->concave_shape_, {});
	});
}

physics_node::~physics_node()
{
	if (!this->rocktree_)
	{
		return;
	}

	this->rocktree_->access_physics([this](reactphysics3d::PhysicsCommon& common, reactphysics3d::PhysicsWorld& world)
	{
		if (this->body_)
		{
			world.destroyCollisionBody(this->body_);
			this->body_ = nullptr;
		}

		if (this->concave_shape_)
		{
			common.destroyConcaveMeshShape(this->concave_shape_);
			this->concave_shape_ = nullptr;
		}

		if (this->triangle_mesh_)
		{
			common.destroyTriangleMesh(this->triangle_mesh_);
			this->triangle_mesh_ = nullptr;
		}
	});
}

node::node(rocktree& rocktree, const bulk& parent, const uint32_t epoch, std::string path, const texture_format format,
           std::optional<uint32_t> imagery_epoch, const bool is_leaf)
	: rocktree_object(rocktree, &parent)
	  , epoch_(epoch)
	  , path_(std::move(path))
	  , format_(format)
	  , imagery_epoch_(std::move(imagery_epoch))
	  , is_leaf_(is_leaf)
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

	this->vertices_ = 0;
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
			throw std::runtime_error("Unsupported texture format: " + std::to_string(texture.format()));
		}

		m.texture_width = static_cast<int>(texture.width());
		m.texture_height = static_cast<int>(texture.height());

		this->vertices_ += m.vertices.size();
		this->meshes.emplace_back(std::move(m));
	}

	this->meshes.shrink_to_fit();

	if (this->is_leaf_ && !this->meshes.empty())
	{
		this->physics_node_.emplace(this->get_rocktree(), this->meshes, this->matrix_globe_from_mesh);
	}
}

void node::clear()
{
	this->physics_node_ = std::nullopt;
	this->meshes.clear();
	this->draw_time_ = {};
	this->buffer_state_ = buffer_state::unbuffered;
	this->vertices_ = 0;
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
