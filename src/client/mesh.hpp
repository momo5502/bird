#pragma once
#include "shader_context.hpp"
#include "gl_objects.hpp"

enum class texture_format : int
{
	rgb,
	dxt1,
};

#pragma pack(push, 1)
struct vertex
{
	uint8_t x, y, z; // position
	uint8_t w; // octant mask
	uint16_t u, v; // texture coordinates
};
#pragma pack(pop)
static_assert((sizeof(vertex) == 8), "vertex size must be 8");

struct mesh_data
{
	glm::vec2 uv_offset{};
	glm::vec2 uv_scale{};

	std::vector<vertex> vertices{};
	std::vector<uint16_t> indices{};
	std::vector<uint8_t> texture{};
	texture_format format{};
	int texture_width{};
	int texture_height{};
};

class mesh_buffers
{
public:
	mesh_buffers(gl_bufferer& bufferer, const mesh_data& mesh);

	void draw(const shader_context& ctx, uint8_t octant_mask, const mesh_data& mesh) const;

private:
	gl_object vertex_buffer_{};
	gl_object index_buffer_{};
	gl_object texture_buffer_{};
};

class mesh
{
public:
	mesh(mesh_data mesh_data);

	void draw(const shader_context& ctx, uint8_t octant_mask) const;
	void unbuffer();
	void buffer(gl_bufferer& bufferer);

private:
	mesh_data mesh_data_{};
	std::optional<mesh_buffers> buffered_mesh_{};
};
