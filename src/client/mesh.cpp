#include "std_include.hpp"

#include "mesh.hpp"
#include "gl_objects.hpp"

#include <utils/finally.hpp>

namespace
{
	void create_mesh_texture(const mesh_data& mesh)
	{
		switch (mesh.format)
		{
		case texture_format::rgb:
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, mesh.texture_width, mesh.texture_height, 0, GL_RGB, GL_UNSIGNED_BYTE,
			             mesh.texture.data());
			break;
		case texture_format::dxt1:
			glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, mesh.texture_width,
			                       mesh.texture_height, 0, static_cast<GLsizei>(mesh.texture.size()),
			                       mesh.texture.data());
			break;
		}
	}
}

mesh::mesh(mesh_data mesh_data)
	: mesh_data_(std::move(mesh_data))
{
}

void mesh::unbuffer()
{
	this->buffered_mesh_ = {};
}

void mesh::buffer(gl_bufferer& bufferer)
{
	if (!this->buffered_mesh_)
	{
		this->buffered_mesh_.emplace(bufferer, this->mesh_data_);
	}
}

mesh_buffers::mesh_buffers(gl_bufferer& bufferer, const mesh_data& mesh)
{
	const auto _ = utils::finally([]
	{
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	});

	this->vertex_buffer_ = bufferer.create_buffer();
	glBindBuffer(GL_ARRAY_BUFFER, this->vertex_buffer_);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizei>(mesh.vertices.size() * sizeof(vertex)),
	             mesh.vertices.data(), GL_STATIC_DRAW);

	this->index_buffer_ = bufferer.create_buffer();
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->index_buffer_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizei>(mesh.indices.size() * sizeof(unsigned short)),
	             mesh.indices.data(), GL_STATIC_DRAW);

	this->texture_buffer_ = bufferer.create_texture();
	glBindTexture(GL_TEXTURE_2D, this->texture_buffer_);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	create_mesh_texture(mesh);
}

void mesh_buffers::draw(const mesh_data& mesh, const shader_context& ctx) const
{
	glUniform2fv(ctx.uv_offset_loc, 1, &mesh.uv_offset[0]);
	glUniform2fv(ctx.uv_scale_loc, 1, &mesh.uv_scale[0]);

	glUniform1i(ctx.texture_loc, 0);

	glBindTexture(GL_TEXTURE_2D, this->texture_buffer_);
	glBindBuffer(GL_ARRAY_BUFFER, this->vertex_buffer_);

	glVertexAttribPointer(ctx.position_loc, 3, GL_UNSIGNED_BYTE, GL_FALSE, 8, nullptr);
	glVertexAttribPointer(ctx.octant_loc, 1, GL_UNSIGNED_BYTE, GL_FALSE, 8, reinterpret_cast<void*>(3));
	glVertexAttribPointer(ctx.texcoords_loc, 2, GL_UNSIGNED_SHORT, GL_FALSE, 8, reinterpret_cast<void*>(4));

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->index_buffer_);
	glDrawElements(GL_TRIANGLE_STRIP, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_SHORT, nullptr);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}
