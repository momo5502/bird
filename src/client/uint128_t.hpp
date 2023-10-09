#pragma once

#include <cstdint>
#include <cstring>

/*****************************************************************************
 *
 ****************************************************************************/

template <typename Base>
class GenericUInt {
public:
  using SelfType = GenericUInt<Base>;

  /*****************************************************************************
   *
   ****************************************************************************/

  GenericUInt(const SelfType&) = default;
  GenericUInt(SelfType&&) noexcept = default;

  SelfType& operator=(const SelfType&) = default;
  SelfType& operator=(SelfType&&) noexcept = default;

  /*****************************************************************************
   *
   ****************************************************************************/

  template <typename T, std::enable_if_t<std::is_integral<T>::value, bool> = true>
  GenericUInt(const T& value)
  {
    mData[0] = Base(value);
    mData[1] = Base{};
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  template <typename T, std::enable_if_t<sizeof(T) < sizeof(Base), bool> = true>
  GenericUInt(const GenericUInt<T>& value)
  {
    mData[0] = Base(value);
    mData[1] = Base{};
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  template <typename T, std::enable_if_t<sizeof(T) < sizeof(Base), bool> = true>
  SelfType& operator=(const GenericUInt<T>& value)
  {
    mData[0] = Base(value);
    mData[1] = Base{};

    return *this;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  GenericUInt(Base lowerBits = 0, Base higherBits = 0)
  {
    static_assert(sizeof(*this) == (sizeof(Base) * 2), "Bad size");

    mData[0] = lowerBits;
    mData[1] = higherBits;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  bool operator==(const SelfType& obj) const
  {
    return mData[0] == obj.mData[0] && mData[1] == obj.mData[1];
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  bool operator!=(const SelfType& obj) const
  {
    return !(*this == obj);
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  bool operator<(const SelfType& obj) const
  {
    if (mData[1] != obj.mData[1]) {
      return mData[1] < obj.mData[1];
    }

    return mData[0] < obj.mData[0];
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  bool operator>(const SelfType& obj) const
  {
    return obj < *this;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  bool operator<=(const SelfType& obj) const
  {
    return *this == obj || *this < obj;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  bool operator>=(const SelfType& obj) const
  {
    return *this == obj || *this > obj;
  }

  /*****************************************************************************
   * @brief It seems that - at least on Intel - shift operands are modulo'ed
   *        on overflow.
   *        So  1ULL << 64 == 1ULL << 0
   *        and 1ULL << 65 == 1ULL << 1
   *        Therefore we need to handle that. Especially on edge cases
   *        when shifting 0 bytes. The carry operation results in a right
   *        shift of 64, which is not the same as zeroing the value.
   ****************************************************************************/

  SelfType operator<<(uint64_t shift) const
  {
    constexpr auto baseBits = (sizeof(Base) * 8);
    constexpr auto selfBits = (sizeof(SelfType) * 8);

    shift %= selfBits;

    if (shift == 0) {
      return *this;
    }

    SelfType newValue{};
    if (shift >= baseBits) {
      newValue.mData[0] = 0;
      newValue.mData[1] = mData[0] << (shift - baseBits);
    }
    else {
      newValue.mData[0] = mData[0] << shift;
      newValue.mData[1] = mData[1] << shift;

      const auto inverseShift = (baseBits - shift);
      newValue.mData[1] |= mData[0] >> inverseShift;
    }

    return newValue;
  }

  /*****************************************************************************
   * @brief Same as left shift...
   ****************************************************************************/

  SelfType operator>>(uint64_t shift) const
  {
    constexpr auto baseBits = (sizeof(Base) * 8);
    constexpr auto selfBits = (sizeof(SelfType) * 8);

    shift %= selfBits;

    if (shift == 0) {
      return *this;
    }

    SelfType newValue{};
    if (shift >= baseBits) {
      newValue.mData[1] = 0;
      newValue.mData[0] = mData[1] >> (shift - baseBits);
    }
    else {
      newValue.mData[1] = mData[1] >> shift;
      newValue.mData[0] = mData[0] >> shift;
      newValue.mData[0] |= mData[1] << (baseBits - shift);
    }

    return newValue;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType operator|(const SelfType& obj) const
  {
    SelfType newValue{};
    newValue.mData[0] = mData[0] | obj.mData[0];
    newValue.mData[1] = mData[1] | obj.mData[1];

    return newValue;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType& operator|=(const SelfType& obj)
  {
    *this = *this | obj;
    return *this;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType operator&(const SelfType& obj) const
  {
    SelfType newValue{};
    newValue.mData[0] = mData[0] & obj.mData[0];
    newValue.mData[1] = mData[1] & obj.mData[1];

    return newValue;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType& operator&=(const SelfType& obj)
  {
    *this = *this & obj;
    return *this;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType operator^(const SelfType& obj) const
  {
    SelfType newValue{};
    newValue.mData[0] = mData[0] ^ obj.mData[0];
    newValue.mData[1] = mData[1] ^ obj.mData[1];

    return newValue;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType& operator^=(const SelfType& obj)
  {
    *this = *this ^ obj;
    return *this;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType operator+(const SelfType& obj) const
  {
    SelfType newValue{};

    newValue.mData[0] = mData[0] + obj.mData[0];
    newValue.mData[1] = mData[1] + obj.mData[1];

    if (newValue.mData[0] < mData[0] || newValue.mData[0] < obj.mData[0]) {
      newValue.mData[1] += 1;
    }

    return newValue;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType& operator+=(const SelfType& obj)
  {
    *this = *this + obj;
    return *this;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType operator~() const
  {
    Base maxBaseValue{};
    memset(reinterpret_cast<void*>(&maxBaseValue), 0xFF, sizeof(maxBaseValue));

    return *this ^ SelfType(maxBaseValue, maxBaseValue);
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType operator-() const
  {
    return (~(*this)) + 1;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType operator-(const SelfType& obj) const
  {
    return *this + (-obj);
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  SelfType& operator-=(const SelfType& obj)
  {
    *this = *this - obj;
    return *this;
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  Base Low() const
  {
    return mData[0];
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  Base High() const
  {
    return mData[1];
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  explicit operator uint64_t() const
  {
    return mData[0];
  }

  /*****************************************************************************
   *
   ****************************************************************************/

  explicit operator uint32_t() const
  {
    return static_cast<uint32_t>(mData[0]);
  }

private:
  Base mData[2]{};
};

/*****************************************************************************
 *
 ****************************************************************************/

using uint128_t = GenericUInt<uint64_t>;
using uint256_t = GenericUInt<uint128_t>;
using uint512_t = GenericUInt<uint256_t>;