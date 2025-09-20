#pragma once
#include "shader_context.hpp"
#include "gl_objects.hpp"

enum class texture_format : int
{
    rgb,
    dxt1,
};

template <typename T>
struct vec3
{
    T x{};
    T y{};
    T z{};
};

#pragma pack(push, 1)
struct vertex
{
    vec3<uint8_t> position{};
    vec3<uint8_t> normal{0x7F, 0x7F, 0x7F};
    uint8_t octant_mask{}; // octant mask
    uint16_t u{};
    uint16_t v{};
};
#pragma pack(pop)

static_assert((sizeof(vertex) == 11), "vertex size must be 8");

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
    mesh_buffers(gl_bufferer& bufferer, const shader_context& ctx, const mesh_data& mesh);

    void draw(const mesh_data& mesh, const shader_context& ctx) const;

  private:
    mutable gl_object vao_{};
    gl_object vertex_buffer_{};
    gl_object index_buffer_{};
    gl_object texture_buffer_{};

    void ensure_vao_existance(const shader_context& ctx) const;
};

class mesh
{
  public:
    mesh(const mesh_data& mesh_data);

    template <typename... Args>
    void draw(Args&&... args) const
    {
        if (this->buffered_mesh_)
        {
            this->buffered_mesh_->draw(*this->mesh_data_, std::forward<Args>(args)...);
        }
    }

    void unbuffer();
    void buffer(gl_bufferer& bufferer, const shader_context& ctx);

    const mesh_data& get_mesh_data() const
    {
        return *this->mesh_data_;
    }

  private:
    const mesh_data* mesh_data_{};
    std::optional<mesh_buffers> buffered_mesh_{};
};
