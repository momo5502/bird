#pragma once

enum class texture_format : int
{
	rgb,
	dxt1,
};

struct gl_ctx_t
{
	GLuint program;
	GLint transform_loc;
	GLint uv_offset_loc;
	GLint uv_scale_loc;
	GLint octant_mask_loc;
	GLint texture_loc;
	GLint position_loc;
	GLint octant_loc;
	GLint texcoords_loc;
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
	std::vector<vertex> vertices{};
	std::vector<uint16_t> indices{};

	glm::vec2 uv_offset{};
	glm::vec2 uv_scale{};

	std::vector<uint8_t> texture{};
	texture_format format{};
	int texture_width{};
	int texture_height{};
};

class mesh_buffers
{
public:
	mesh_buffers(const mesh_data& mesh);
	~mesh_buffers();

	mesh_buffers(mesh_buffers&&) = delete;
	mesh_buffers(const mesh_buffers&) = delete;

	mesh_buffers& operator=(mesh_buffers&&) = delete;
	mesh_buffers& operator=(const mesh_buffers&) = delete;

	void draw(const gl_ctx_t& ctx, uint8_t octant_mask, const mesh_data& mesh) const;

private:
	GLuint vertex_buffer_{};
	GLuint index_buffer_{};
	GLuint texture_buffer_{};
};

class mesh
{
public:
	mesh(mesh_data mesh_data);

	void draw(const gl_ctx_t& ctx, uint8_t octant_mask);
	void unbuffer();

private:
	mesh_data mesh_data_{};
	std::unique_ptr<mesh_buffers> buffered_mesh_{};
};
