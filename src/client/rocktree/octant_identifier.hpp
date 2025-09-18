#pragma once

#include "../uint128_t.hpp"

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

	octant_identifier(const std::string_view& str)
	{
		for (auto& val : str)
		{
			this->add(val - '0');
		}
	}

	octant_identifier(const std::string& str)
		: octant_identifier(std::string_view(str))
	{
	}

	size_t size() const
	{
		constexpr auto size_bit = (sizeof(Base) - 1) * 8;
		return static_cast<size_t>(static_cast<uint64_t>(this->value_ >> size_bit));
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

	octant_identifier operator+(const octant_identifier& value) const
	{
		octant_identifier new_value = *this;

		const auto current_size = new_value.size();
		const auto value_bit = current_size * 3;
		const auto new_value_bits = value.value_ << value_bit;

		new_value.value_ |= new_value_bits;
		new_value.set_size(current_size + value.size());


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

	Base get_value() const
	{
		return this->value_;
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

template<>
struct std::hash<octant_identifier<>>
{
	inline std::size_t operator()(const octant_identifier<>& o) const noexcept
	{
		std::size_t h1 = std::hash<uint64_t>{}(o.get_value().low());
		std::size_t h2 = std::hash<uint64_t>{}(o.get_value().high());
		return h1 ^ (h2 << 1); // or use boost::hash_combine
	}
};