#include "std_include.hpp"

#include "shader.hpp"

#include <utils/finally.hpp>

namespace
{
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

	void compile_shader(const GLuint shader, const std::string_view& code)
	{
		perform_shader_compilation(shader, code);

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
	                             const std::string_view& fragment_shader_code)
	{
		const auto vertex_shader = glCreateShader(GL_VERTEX_SHADER);
		const auto vertex_destructor = create_shader_destructor(vertex_shader);
		compile_shader(vertex_shader, vertex_shader_code);

		const auto fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
		const auto fragment_destructor = create_shader_destructor(fragment_shader);
		compile_shader(fragment_shader, fragment_shader_code);

		const auto program = glCreateProgram();

		glAttachShader(program, vertex_shader);
		glAttachShader(program, fragment_shader);

		glLinkProgram(program);

		glDetachShader(program, vertex_shader);
		glDetachShader(program, fragment_shader);

		return program;
	}
}

shader::shader(const std::string_view& vertex_shader, const std::string_view& fragment_shader)
	: program_(create_shader_program(vertex_shader, fragment_shader), glDeleteProgram)
{
}

GLuint shader::get_program() const
{
	return this->program_.get();
}
