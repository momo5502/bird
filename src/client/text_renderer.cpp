#include "std_include.hpp"

#include "gl_objects.hpp"
#include "text_renderer.hpp"

namespace
{
	character create_character(const char glyph, const FT_Face face)
	{
		if (FT_Load_Char(face, glyph, FT_LOAD_RENDER))
		{
			throw std::runtime_error("Failed to render character: " + std::to_string(glyph));
		}

		character c{};
		c.texture = create_texture();

		glBindTexture(GL_TEXTURE_2D, c.texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, static_cast<GLsizei>(face->glyph->bitmap.width),
		             static_cast<GLsizei>(face->glyph->bitmap.rows), 0, GL_RED, GL_UNSIGNED_BYTE,
		             face->glyph->bitmap.buffer);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		c.size = glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows);
		c.bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top);
		c.advance = static_cast<uint32_t>(face->glyph->advance.x);

		return c;
	}

	std::string_view get_vertex_shader()
	{
		return R"code(
			#version 330 core
			layout (location = 0) in vec4 vertex; // <vec2 pos, vec2 tex>
			out vec2 TexCoords;

			uniform mat4 projection;

			void main()
			{
			    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
			    TexCoords = vertex.zw;
			}
			)code";
	}

	std::string_view get_fragment_shader()
	{
		return R"code(
			#version 330 core
			in vec2 TexCoords;
			out vec4 color;

			uniform sampler2D text;
			uniform vec4 textColor;

			void main()
			{    
			    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);
			    color = textColor * sampled;
			}
			)code";
	}

	gl_object create_vertex_buffer()
	{
		auto buffer = create_buffer();
		glBindBuffer(GL_ARRAY_BUFFER, buffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		return buffer;
	}
}

text_renderer::text_renderer(const std::string_view& font, const size_t font_size)
	: shader_(get_vertex_shader(), get_fragment_shader())
	  , vertex_buffer_(create_vertex_buffer())
{
	if (FT_Init_FreeType(&this->ft_))
	{
		throw std::runtime_error("Failed to initialize freetype");
	}

	if (FT_New_Memory_Face(this->ft_, reinterpret_cast<const FT_Byte*>(font.data()), static_cast<FT_Long>(font.size()),
	                       0, &this->face_))
	{
		FT_Done_FreeType(this->ft_);
		throw std::runtime_error("Failed to initialize typeface");
	}

	FT_Set_Pixel_Sizes(this->face_, 0, static_cast<FT_UInt>(font_size));
}

text_renderer::~text_renderer()
{
	this->destroy();
}

text_renderer::text_renderer(text_renderer&& obj) noexcept
{
	this->operator=(std::move(obj));
}

text_renderer& text_renderer::operator=(text_renderer&& obj) noexcept
{
	if (this != &obj)
	{
		this->destroy();

		this->ft_ = obj.ft_;
		this->face_ = obj.face_;

		this->characters_ = std::move(obj.characters_);
		this->shader_ = std::move(obj.shader_);
		this->vertex_buffer_ = std::move(obj.vertex_buffer_);

		obj.ft_ = nullptr;
		obj.face_ = nullptr;
	}

	return *this;
}

void text_renderer::draw(const std::string_view& text, float x, float y, const float scale, const glm::vec4 color)
{
	glClear(GL_DEPTH_BUFFER_BIT);

	this->shader_.use();
	const auto program = this->shader_.get_program();

	GLint viewport[4]{};
	glGetIntegerv(GL_VIEWPORT, viewport);

	const auto width = viewport[2];
	const auto height = viewport[3];

	y = static_cast<float>(height) - y;

	glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height));

	const auto projection_uniform = glGetUniformLocation(program, "projection");
	glUniformMatrix4fv(projection_uniform, 1, GL_FALSE, &projection[0][0]);

	const auto color_uniform = glGetUniformLocation(program, "textColor");
	glUniform4f(color_uniform, color.r, color.g, color.b, color.a);
	glActiveTexture(GL_TEXTURE0);

	glBindBuffer(GL_ARRAY_BUFFER, this->vertex_buffer_);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

	for (const auto chr : text)
	{
		const auto& ch = this->get_character(chr);

		const auto xpos = x + static_cast<float>(ch.bearing.x) * scale;
		const auto ypos = y - static_cast<float>(ch.size.y - ch.bearing.y) * scale;

		const auto w = static_cast<float>(ch.size.x) * scale;
		const auto h = static_cast<float>(ch.size.y) * scale;

		const float vertices[6][4] =
		{
			{xpos, ypos + h, 0.0f, 0.0f},
			{xpos, ypos, 0.0f, 1.0f},
			{xpos + w, ypos, 1.0f, 1.0f},

			{xpos, ypos + h, 0.0f, 0.0f},
			{xpos + w, ypos, 1.0f, 1.0f},
			{xpos + w, ypos + h, 1.0f, 0.0f},
		};

		glBindTexture(GL_TEXTURE_2D, ch.texture);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

		glDrawArrays(GL_TRIANGLES, 0, 6);
		x += static_cast<float>(ch.advance >> 6) * scale;
	}

	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

const character& text_renderer::get_character(const char c)
{
	auto entry = this->characters_.find(c);
	if (entry == this->characters_.end())
	{
		entry = this->characters_.try_emplace(c, create_character(c, this->face_)).first;
	}

	return entry->second;
}

void text_renderer::destroy()
{
	this->characters_.clear();

	if (this->face_)
	{
		FT_Done_Face(this->face_);
		this->face_ = nullptr;
	}

	if (this->ft_)
	{
		FT_Done_FreeType(this->ft_);
		this->ft_ = nullptr;
	}
}
