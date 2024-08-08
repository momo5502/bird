#include "std_include.hpp"

#include "shader.hpp"

#include <utils/finally.hpp>

namespace
{
	std::string_view get_vertex_fixup()
	{
#ifdef __APPLE__
		return "#version 150\n#define varying out\n#define attribute in\n";
#else
		return "";
#endif
	}


	std::string_view get_fragment_fixup()
	{
#ifdef __APPLE__
		return "#version 150\n#define varying in\n#define texture2D texture\n#define textureCube texture\n#define gl_FragColor fragColor\nout vec4 fragColor;\n";
#else
		return "";
#endif
	}

	std::string get_shader_info_log(const GLuint shader)
	{
		GLint max_len = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &max_len);

		std::string error_log{};
		error_log.resize(max_len);

		glGetShaderInfoLog(shader, max_len, &max_len, error_log.data());

		error_log.resize(max_len);

		return error_log;
	}

	bool is_shader_compiled(const GLuint shader)
	{
		GLint is_compiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &is_compiled);
		return is_compiled == GL_TRUE;
	}

	void perform_shader_compilation(const GLuint shader, const std::string_view& code)
	{
		const char* shader_code = code.data();
		const auto shader_length = static_cast<GLint>(code.size());

		glShaderSource(shader, 1, &shader_code, &shader_length);
		glCompileShader(shader);
	}

	void compile_shader(const GLuint shader, const std::string_view& code, const std::string_view& fixup,
	                    const bool apply_fixup)
	{
		if (apply_fixup)
		{
			perform_shader_compilation(shader, std::string(fixup).append(code));
		}
		else
		{
			perform_shader_compilation(shader, code);
		}

		if (!is_shader_compiled(shader))
		{
			throw std::runtime_error(get_shader_info_log(shader));
		}
	}

	auto create_shader_destructor(const GLuint shader)
	{
		return utils::finally([shader]
		{
			glDeleteShader(shader);
		});
	}

	GLuint create_shader_program(const std::string_view& vertex_shader_code,
	                             const std::string_view& fragment_shader_code, const bool apply_fixups)
	{
		const auto vertex_shader = glCreateShader(GL_VERTEX_SHADER);
		const auto vertex_destructor = create_shader_destructor(vertex_shader);
		compile_shader(vertex_shader, vertex_shader_code, get_vertex_fixup(), apply_fixups);

		const auto fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
		const auto fragment_destructor = create_shader_destructor(fragment_shader);
		compile_shader(fragment_shader, fragment_shader_code, get_fragment_fixup(), apply_fixups);

		const auto program = glCreateProgram();

		glAttachShader(program, vertex_shader);
		glAttachShader(program, fragment_shader);

		glLinkProgram(program);

		glDetachShader(program, vertex_shader);
		glDetachShader(program, fragment_shader);

		return program;
	}
}

shader::shader(const std::string_view& vertex_shader, const std::string_view& fragment_shader, const bool apply_fixups)
	: program_(create_shader_program(vertex_shader, fragment_shader, apply_fixups), glDeleteProgram)
{
}

GLuint shader::get_program() const
{
	return this->program_.get();
}

void shader::use() const
{
	glUseProgram(this->program_);
}
