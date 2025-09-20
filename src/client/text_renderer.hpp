#pragma once
#include "gl_object.hpp"
#include "shader.hpp"

struct character
{
    gl_object texture;
    glm::ivec2 size;
    glm::ivec2 bearing;
    uint32_t advance;
};

class text_renderer
{
  public:
    text_renderer(const std::string_view& font, size_t font_size);
    ~text_renderer();

    text_renderer(const text_renderer&) = delete;
    text_renderer& operator=(const text_renderer&) = delete;

    text_renderer(text_renderer&& obj) noexcept;
    text_renderer& operator=(text_renderer&& obj) noexcept;

    void draw(const std::string_view& text, float x, float y, float scale, glm::vec4 color);

  private:
    shader shader_{};
    gl_object vao_{};
    gl_object vertex_buffer_{};

    FT_Library ft_{nullptr};
    FT_Face face_{nullptr};

    std::map<char, character> characters_{};

    const character& get_character(const char c);

    void destroy();
};
