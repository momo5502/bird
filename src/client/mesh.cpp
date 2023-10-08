#include "std_include.hpp"

#include "mesh.hpp"

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

void mesh::draw(const gl_ctx_t& ctx, const uint8_t octant_mask)
{
	if (!this->buffered_mesh_)
	{
		this->buffered_mesh_ = std::make_unique<mesh_buffers>(this->mesh_data_);
	}

	this->buffered_mesh_->draw(ctx, octant_mask, this->mesh_data_);
}

void mesh::unbuffer()
{
	this->buffered_mesh_ = {};
}

mesh_buffers::mesh_buffers(const mesh_data& mesh)
{
	glGenBuffers(1, &this->vertex_buffer_);
	glBindBuffer(GL_ARRAY_BUFFER, this->vertex_buffer_);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizei>(mesh.vertices.size() * sizeof(vertex)),
	             mesh.vertices.data(), GL_STATIC_DRAW);

	glGenBuffers(1, &this->index_buffer_);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->index_buffer_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizei>(mesh.indices.size() * sizeof(unsigned short)),
	             mesh.indices.data(), GL_STATIC_DRAW);

	glGenTextures(1, &this->texture_buffer_);
	glBindTexture(GL_TEXTURE_2D, this->texture_buffer_);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	create_mesh_texture(mesh);
}

mesh_buffers::~mesh_buffers()
{
	glDeleteTextures(1, &this->texture_buffer_);
	glDeleteBuffers(1, &this->index_buffer_);
	glDeleteBuffers(1, &this->vertex_buffer_);
}

void mesh_buffers::draw(const gl_ctx_t& ctx, const uint8_t octant_mask, const mesh_data& mesh) const
{
	glUniform2fv(ctx.uv_offset_loc, 1, &mesh.uv_offset[0]);
	glUniform2fv(ctx.uv_scale_loc, 1, &mesh.uv_scale[0]);

	const int v[8] =
	{
		(octant_mask >> 0) & 1, (octant_mask >> 1) & 1, (octant_mask >> 2) & 1, (octant_mask >> 3) & 1,
		(octant_mask >> 4) & 1, (octant_mask >> 5) & 1, (octant_mask >> 6) & 1, (octant_mask >> 7) & 1
	};

	glUniform1iv(ctx.octant_mask_loc, 8, v);
	glUniform1i(ctx.texture_loc, 0);
	glBindTexture(GL_TEXTURE_2D, this->texture_buffer_);
	glBindBuffer(GL_ARRAY_BUFFER, this->vertex_buffer_);

	glVertexAttribPointer(ctx.position_loc, 3, GL_UNSIGNED_BYTE, GL_FALSE, 8, nullptr);
	glVertexAttribPointer(ctx.octant_loc, 1, GL_UNSIGNED_BYTE, GL_FALSE, 8, reinterpret_cast<void*>(3));
	glVertexAttribPointer(ctx.texcoords_loc, 2, GL_UNSIGNED_SHORT, GL_FALSE, 8, reinterpret_cast<void*>(4));

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->index_buffer_);
	glDrawElements(GL_TRIANGLE_STRIP, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_SHORT, nullptr);
}
