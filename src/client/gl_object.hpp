#pragma once

class gl_object
{
public:
	gl_object() = default;
	gl_object(GLuint object, std::function<void(GLuint)> destructor);

	~gl_object();

	gl_object(const gl_object&) = delete;
	gl_object& operator=(const gl_object&) = delete;

	gl_object(gl_object&& obj) noexcept;
	gl_object& operator=(gl_object&& obj) noexcept;

	GLuint get() const;
	operator GLuint() const;

private:
	std::function<void(GLuint)> destructor_{};
	std::optional<GLuint> object_{};

	void destroy();
};
