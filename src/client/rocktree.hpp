#pragma once

#include "task_manager.hpp"
#include "mesh.hpp"

#include "uint128_t.hpp"
#include <utils/http.hpp>

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

	bool is_failed() const
	{
		return this->state_ == state::failed;
	}

	bool is_in_final_state() const
	{
		const auto state = this->state_.load();
		return state == state::ready || state == state::failed || state == state::deleting;
	}

	bool is_ready_or_failed() const
	{
		const auto state = this->state_.load();
		return state == state::ready || state == state::failed;
	}

	bool is_fetching() const
	{
		return this->state_ == state::fetching;
	}

	bool can_be_used()
	{
		const auto state = this->state_.load();
		if (state == state::deleting || state == state::failed)
		{
			return false;
		}

		this->last_use_ = std::chrono::steady_clock::now();

		if (state == state::ready)
		{
			return true;
		}

		this->fetch();
		return false;
	}

	bool mark_for_deletion()
	{
		if (this->is_in_final_state())
		{
			this->state_ = state::deleting;
			return true;
		}

		auto expected = state::fresh;
		return this->state_.compare_exchange_strong(expected, state::deleting);
	}

	bool try_perform_deletion()
	{
		if (this->state_ != state::deleting)
		{
			return false;
		}

		this->clear();
		this->state_ = state::fresh;
		return true;
	}

	bool was_used_within(const std::chrono::milliseconds& duration) const;

	void fetch();

protected:
	virtual void populate() = 0;
	virtual void clear() = 0;

	void mark_done(bool success);

	task_manager& get_manager() const;
	utils::http::downloader& get_downloader() const;

	virtual bool is_high_priority() const
	{
		return false;
	}

	void fetch_data(const std::string_view& path, utils::http::result_function function, bool prefer_cache = true);

private:
	enum class state
	{
		fresh,
		fetching,
		ready,
		deleting,
		failed,
	};

	std::atomic<std::chrono::steady_clock::time_point> last_use_{std::chrono::steady_clock::now()};
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

	void buffer_meshes();
	bool is_buffered() const;

	std::vector<mesh> meshes{};

private:
	std::atomic_bool buffered_{false};
	uint32_t epoch_{};
	std::string path_{};

	texture_format format_{};
	std::optional<uint32_t> imagery_epoch_{};

	void populate() override;
	void clear() override;
};

class bulk final : public rocktree_object
{
public:
	bulk(rocktree& rocktree, uint32_t epoch, std::string path = {});

	glm::dvec3 head_node_center{};
	std::map<octant_identifier<>, std::unique_ptr<node>> nodes{};
	std::map<octant_identifier<>, std::unique_ptr<bulk>> bulks{};

	const std::string& get_path() const;

private:
	uint32_t epoch_{};
	std::string path_{};

	void populate() override;
	void clear() override;
};

class planetoid final : public rocktree_object
{
public:
	using rocktree_object::rocktree_object;

	float radius{};
	std::unique_ptr<bulk> root_bulk{};

private:
	void populate() override;
	void clear() override;
};

class rocktree
{
public:
	friend rocktree_object;

	rocktree(std::string planet);
	~rocktree();

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

	utils::http::downloader downloader_{};
	std::jthread downloader_thread_{};

	task_manager task_manager_{};
};
