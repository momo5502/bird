#include "std_include.hpp"

#include "gl_objects.hpp"

namespace
{
	void delete_texture(const GLuint texture)
	{
		glDeleteTextures(1, &texture);
	}

	void delete_buffer(const GLuint buffer)
	{
		glDeleteBuffers(1, &buffer);
	}
}

gl_object create_texture()
{
	GLuint texture{};
	glGenTextures(1, &texture);

	return {texture, delete_texture};
}

gl_object create_buffer()
{
	GLuint buffer{};
	glGenBuffers(1, &buffer);

	return {buffer, delete_buffer};
}
