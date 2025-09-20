#pragma once

#include <cstdint>
#include <cstring>

template <typename Base>
class generic_uint
{
  public:
    using self_type = generic_uint<Base>;

    generic_uint(const self_type&) = default;
    generic_uint(self_type&&) noexcept = default;

    self_type& operator=(const self_type&) = default;
    self_type& operator=(self_type&&) noexcept = default;

    template <typename T, std::enable_if_t<std::is_integral_v<T>, bool> = true>
    generic_uint(const T& value)
    {
        data_[0] = Base(value);
        data_[1] = Base{};
    }

    template <typename T, std::enable_if_t<sizeof(T) < sizeof(Base), bool> = true>
    generic_uint(const generic_uint<T>& value)
    {
        data_[0] = Base(value);
        data_[1] = Base{};
    }

    template <typename T, std::enable_if_t<sizeof(T) < sizeof(Base), bool> = true>
    self_type& operator=(const generic_uint<T>& value)
    {
        data_[0] = Base(value);
        data_[1] = Base{};

        return *this;
    }

    generic_uint(Base lower_bits = 0, Base higher_bits = 0)
    {
        static_assert(sizeof(*this) == (sizeof(Base) * 2), "Bad size");

        data_[0] = lower_bits;
        data_[1] = higher_bits;
    }

    bool operator==(const self_type& obj) const
    {
        return data_[0] == obj.data_[0] && data_[1] == obj.data_[1];
    }

    bool operator!=(const self_type& obj) const
    {
        return !(*this == obj);
    }

    bool operator<(const self_type& obj) const
    {
        if (data_[1] != obj.data_[1])
        {
            return data_[1] < obj.data_[1];
        }

        return data_[0] < obj.data_[0];
    }

    bool operator>(const self_type& obj) const
    {
        return obj < *this;
    }

    bool operator<=(const self_type& obj) const
    {
        return *this == obj || *this < obj;
    }

    bool operator>=(const self_type& obj) const
    {
        return *this == obj || *this > obj;
    }

    self_type operator<<(uint64_t shift) const
    {
        constexpr auto base_bits = (sizeof(Base) * 8);
        constexpr auto self_bits = (sizeof(self_type) * 8);

        shift %= self_bits;

        if (shift == 0)
        {
            return *this;
        }

        self_type new_value{};
        if (shift >= base_bits)
        {
            new_value.data_[0] = 0;
            new_value.data_[1] = data_[0] << (shift - base_bits);
        }
        else
        {
            new_value.data_[0] = data_[0] << shift;
            new_value.data_[1] = data_[1] << shift;

            const auto inverseShift = (base_bits - shift);
            new_value.data_[1] |= data_[0] >> inverseShift;
        }

        return new_value;
    }

    self_type operator>>(uint64_t shift) const
    {
        constexpr auto base_bits = (sizeof(Base) * 8);
        constexpr auto self_bits = (sizeof(self_type) * 8);

        shift %= self_bits;

        if (shift == 0)
        {
            return *this;
        }

        self_type new_value{};
        if (shift >= base_bits)
        {
            new_value.data_[1] = 0;
            new_value.data_[0] = data_[1] >> (shift - base_bits);
        }
        else
        {
            new_value.data_[1] = data_[1] >> shift;
            new_value.data_[0] = data_[0] >> shift;
            new_value.data_[0] |= data_[1] << (base_bits - shift);
        }

        return new_value;
    }

    self_type operator|(const self_type& obj) const
    {
        self_type newValue{};
        newValue.data_[0] = data_[0] | obj.data_[0];
        newValue.data_[1] = data_[1] | obj.data_[1];

        return newValue;
    }

    self_type& operator|=(const self_type& obj)
    {
        *this = *this | obj;
        return *this;
    }

    self_type operator&(const self_type& obj) const
    {
        self_type new_value{};
        new_value.data_[0] = data_[0] & obj.data_[0];
        new_value.data_[1] = data_[1] & obj.data_[1];

        return new_value;
    }

    self_type& operator&=(const self_type& obj)
    {
        *this = *this & obj;
        return *this;
    }

    self_type operator^(const self_type& obj) const
    {
        self_type new_value{};
        new_value.data_[0] = data_[0] ^ obj.data_[0];
        new_value.data_[1] = data_[1] ^ obj.data_[1];

        return new_value;
    }

    self_type& operator^=(const self_type& obj)
    {
        *this = *this ^ obj;
        return *this;
    }

    self_type operator+(const self_type& obj) const
    {
        self_type new_value{};

        new_value.data_[0] = data_[0] + obj.data_[0];
        new_value.data_[1] = data_[1] + obj.data_[1];

        if (new_value.data_[0] < data_[0] || new_value.data_[0] < obj.data_[0])
        {
            new_value.data_[1] += 1;
        }

        return new_value;
    }

    self_type& operator+=(const self_type& obj)
    {
        *this = *this + obj;
        return *this;
    }

    self_type operator~() const
    {
        static_assert(std::is_trivially_copyable_v<Base>);

        Base max_base_value{};
        memset(reinterpret_cast<void*>(&max_base_value), 0xFF, sizeof(max_base_value));

        return *this ^ self_type(max_base_value, max_base_value);
    }

    self_type operator-() const
    {
        return (~(*this)) + 1;
    }

    self_type operator-(const self_type& obj) const
    {
        return *this + (-obj);
    }

    self_type& operator-=(const self_type& obj)
    {
        *this = *this - obj;
        return *this;
    }

    Base low() const
    {
        return data_[0];
    }

    Base high() const
    {
        return data_[1];
    }

    explicit operator uint64_t() const
    {
        return data_[0];
    }

    explicit operator uint32_t() const
    {
        return static_cast<uint32_t>(data_[0]);
    }

    explicit operator uint16_t() const
    {
        return static_cast<uint16_t>(data_[0]);
    }

    explicit operator uint8_t() const
    {
        return static_cast<uint8_t>(data_[0]);
    }

  private:
    Base data_[2]{};
};

/*****************************************************************************
 *
 ****************************************************************************/

using uint128_t = generic_uint<uint64_t>;
using uint256_t = generic_uint<uint128_t>;
using uint512_t = generic_uint<uint256_t>;
