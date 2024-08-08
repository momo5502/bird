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

	void delete_vertex_array_object(const GLuint vao)
	{
		glDeleteVertexArrays(1, &vao);
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

gl_object create_vertex_array_object()
{
	GLuint vao = 0;
	glGenVertexArrays(1, &vao);

	return {vao, delete_vertex_array_object};
}

gl_bufferer::~gl_bufferer()
{
	this->perform_cleanup();
}

gl_object gl_bufferer::create_texture()
{
	GLuint texture{};
	glGenTextures(1, &texture);

	return {
		texture, [this](const GLuint obj)
		{
			this->schedule_texture_cleanup(obj);
		}
	};
}

gl_object gl_bufferer::create_buffer()
{
	GLuint buffer{};
	glGenBuffers(1, &buffer);

	return {
		buffer, [this](const GLuint obj)
		{
			this->schedule_buffer_cleanup(obj);
		}
	};
}

void gl_bufferer::perform_cleanup()
{
	this->buffers_.access([](object_vector& vector)
	{
		if (!vector.empty())
		{
			glDeleteBuffers(static_cast<GLsizei>(vector.size()), vector.data());
			vector = {};
		}
	});

	this->textures_.access([](object_vector& vector)
	{
		if (!vector.empty())
		{
			glDeleteTextures(static_cast<GLsizei>(vector.size()), vector.data());
			vector = {};
		}
	});
}

void gl_bufferer::schedule_buffer_cleanup(const GLuint buffer)
{
	this->buffers_.access([buffer](object_vector& vector)
	{
		vector.push_back(buffer);
	});
}

void gl_bufferer::schedule_texture_cleanup(const GLuint texture)
{
	this->textures_.access([texture](object_vector& vector)
	{
		vector.push_back(texture);
	});
}
