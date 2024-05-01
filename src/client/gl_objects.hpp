#pragma once
#include "gl_object.hpp"

#include <utils/concurrency.hpp>

gl_object create_texture();
gl_object create_buffer();

class gl_bufferer
{
public:
	gl_bufferer() = default;
	~gl_bufferer();

	gl_bufferer(const gl_bufferer&) = delete;
	gl_bufferer& operator=(const gl_bufferer&) = delete;

	gl_bufferer(gl_bufferer&&) = delete;
	gl_bufferer& operator=(gl_bufferer&&) = delete;

	gl_object create_texture();
	gl_object create_buffer();

	void perform_cleanup();

private:
	using object_vector = std::vector<GLuint>;

	utils::concurrency::container<object_vector> buffers_{};
	utils::concurrency::container<object_vector> textures_{};

	void schedule_buffer_cleanup(GLuint buffer);
	void schedule_texture_cleanup(GLuint texture);
};
