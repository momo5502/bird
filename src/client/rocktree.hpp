#pragma once

#include "task_manager.hpp"
#include "mesh.hpp"

#include "uint128_t.hpp"

class rocktree;

template <typename Base = uint128_t>
class octant_identifier
{
public:
	static constexpr size_t store_bits = sizeof(uint8_t) * 8;
	static constexpr size_t max_encodable_levels = ((sizeof(Base) * 8) - store_bits) / 3;
	static constexpr size_t max_storable_levels = ((1 << store_bits));
	static constexpr size_t max_levels = std::min(max_encodable_levels, max_storable_levels);

	octant_identifier(const Base& value = {})
		: value_(value)
	{
	}

	octant_identifier(const std::string& str)
	{
		for (auto& val : str)
		{
			this->add(val - '0');
		}
	}

	size_t size() const
	{
		constexpr auto size_bit = (sizeof(Base) - 1) * 8;
		return static_cast<size_t>(this->value_ >> size_bit);
	}

	uint8_t operator[](const size_t index) const
	{
		if (index >= max_levels)
		{
			throw std::runtime_error("Out of bounds access");
		}

		const auto current_bits = index * 3;
		const auto mask_bits = current_bits + 3;

		const auto mask = (Base(1) << mask_bits) - 1;

		const auto value = this->value_ & mask;
		const auto result = value >> current_bits;

		return static_cast<uint8_t>(result);
	}

	octant_identifier operator+(const uint8_t value) const
	{
		octant_identifier new_value = *this;
		new_value.add(value);

		return new_value;
	}

	std::string to_string() const
	{
		const auto current_size = this->size();

		std::string res{};
		res.reserve(current_size);

		for (size_t i = 0; i < current_size; ++i)
		{
			res.push_back(static_cast<char>('0' + (*this)[i]));
		}

		return res;
	}

	octant_identifier substr(const size_t start, const size_t length) const
	{
		const auto end = std::min(start + length, this->size());
		if (start >= end)
		{
			return {};
		}

		const auto new_length = end - start;

		const auto current_bits = end * 3;

		const auto mask = (Base(1) << current_bits) - 1;
		const auto maked_value = this->value_ & mask;

		const auto start_bits = start * 3;
		const auto value = maked_value >> start_bits;

		octant_identifier new_value{};
		new_value.value_ = value;
		new_value.set_size(new_length);

		return new_value;
	}

	bool operator==(const octant_identifier& obj) const
	{
		return this->value_ == obj.value_;
	}

	bool operator!=(const octant_identifier& obj) const
	{
		return !(*this == obj);
	}

	bool operator<(const octant_identifier& obj) const
	{
		return this->value_ < obj.value_;
	}

	bool operator>(const octant_identifier& obj) const
	{
		return obj < *this;
	}

	bool operator<=(const octant_identifier& obj) const
	{
		return *this == obj || *this < obj;
	}

	bool operator>=(const octant_identifier& obj) const
	{
		return *this == obj || *this > obj;
	}

private:
	void set_size(const size_t size)
	{
		if (size > max_levels)
		{
			throw std::runtime_error("Exceeded limit of " + std::to_string(max_levels) + "levels");
		}

		constexpr auto size_bit = (sizeof(Base) - 1) * 8;

		this->value_ &= (Base(1) << size_bit) - 1;
		this->value_ |= Base(size) << size_bit;
	}

	void add(const uint8_t value)
	{
		const auto current_size = this->size();
		const auto value_bit = current_size * 3;
		const auto new_value = Base(value & 7) << value_bit;

		this->value_ |= new_value;
		this->set_size(current_size + 1);
	}

	Base value_{0};
};

class rocktree_object
{
public:
	rocktree_object(rocktree& rocktree);
	virtual ~rocktree_object() = default;

	rocktree_object(rocktree_object&&) = delete;
	rocktree_object(const rocktree_object&) = delete;
	rocktree_object& operator=(rocktree_object&&) = delete;
	rocktree_object& operator=(const rocktree_object&) = delete;

	rocktree& get_rocktree() const
	{
		return *this->rocktree_;
	}

	const std::string& get_planet() const;

	bool is_fresh() const
	{
		return this->state_ == state::fresh;
	}

	bool is_ready() const
	{
		return this->state_ == state::ready;
	}

	bool is_fetching() const
	{
		return this->state_ == state::fetching;
	}

	bool can_be_used()
	{
		if (this->is_ready())
		{
			return true;
		}

		this->fetch();
		return false;
	}

	virtual bool can_be_removed() const
	{
		return !this->is_fetching();
	}

	void fetch();

protected:
	virtual void populate() = 0;

	virtual bool is_high_priority() const
	{
		return false;
	}

private:
	enum class state
	{
		fresh,
		fetching,
		ready,
		failed,
	};

	std::atomic<state> state_{state::fresh};
	rocktree* rocktree_{};
};

struct oriented_bounding_box
{
	glm::dvec3 center{};
	glm::dvec3 extents{};
	glm::dmat3 orientation{};
};

class node final : public rocktree_object
{
public:
	node(rocktree& rocktree, uint32_t epoch, std::string path, texture_format format,
	     std::optional<uint32_t> imagery_epoch);

	bool can_have_data{};
	float meters_per_texel{};
	oriented_bounding_box obb{};
	glm::dmat4 matrix_globe_from_mesh{};

	std::vector<mesh> meshes{};

private:
	uint32_t epoch_{};
	std::string path_{};

	texture_format format_{};
	std::optional<uint32_t> imagery_epoch_{};

	void populate() override;
};

class bulk final : public rocktree_object
{
public:
	bulk(rocktree& rocktree, uint32_t epoch, std::string path = {});

	glm::dvec3 head_node_center{};
	std::map<octant_identifier<>, std::unique_ptr<node>> nodes{};
	std::map<octant_identifier<>, std::unique_ptr<bulk>> bulks{};

	bool can_be_removed() const override;

	const std::string& get_path() const;

private:
	uint32_t epoch_{};
	std::string path_{};

	void populate() override;
};

class planetoid final : public rocktree_object
{
public:
	using rocktree_object::rocktree_object;

	float radius{};
	std::unique_ptr<bulk> root_bulk{};

	bool can_be_removed() const override;

private:
	void populate() override;
};

class rocktree
{
public:
	friend rocktree_object;

	rocktree(std::string planet);

	const std::string& get_planet() const
	{
		return this->planet_;
	}

	planetoid* get_planetoid() const
	{
		return this->planetoid_.get();
	}

private:
	std::string planet_{};
	std::unique_ptr<planetoid> planetoid_{};

	task_manager task_manager_{};
};
