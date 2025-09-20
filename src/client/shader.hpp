#pragma once
#include "gl_object.hpp"

struct scoped_shader
{
    scoped_shader(const GLuint shader)
    {
        glUseProgram(shader);
    }

    ~scoped_shader()
    {
        glUseProgram(0);
    }

    scoped_shader(scoped_shader&&) = delete;
    scoped_shader(const scoped_shader&) = delete;
    scoped_shader& operator=(const scoped_shader&) = delete;
    scoped_shader& operator=(scoped_shader&&) = delete;
};

class shader
{
  public:
    shader() = default;
    shader(const std::string_view& vertex_shader, const std::string_view& fragment_shader, bool apply_fixups = true);

    GLuint get_program() const;

    GLint uniform(const char* name) const;
    GLint attribute(const char* name) const;

    [[nodiscard]] scoped_shader use() const;

  private:
    gl_object program_{};
};
