#pragma once
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

/* Dynamic contiguous views used by the command parser. Keep this small API
 * available in C++17: GCC 9's standard library does not provide std::span. */
template <class T> class RDPSpan {
  public:
    constexpr RDPSpan() noexcept = default;
    constexpr RDPSpan(T *data, std::size_t count) noexcept : data_(data), size_(count) {}

    template <class U, std::size_t N,
              std::enable_if_t<std::is_convertible<U (*)[], T (*)[]>::value, int> = 0>
    constexpr RDPSpan(U (&data)[N]) noexcept : data_(data), size_(N) {}

    template <class Container,
              std::enable_if_t<std::is_convertible<decltype(std::data(std::declval<Container &>())), T *>::value, int> = 0>
    RDPSpan(Container &data) noexcept : data_(std::data(data)), size_(std::size(data)) {}

    template <class Container,
              std::enable_if_t<std::is_convertible<decltype(std::data(std::declval<const Container &>())), T *>::value, int> = 0>
    RDPSpan(const Container &data) noexcept : data_(std::data(data)), size_(std::size(data)) {}

    constexpr T *data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }
    constexpr T *begin() const noexcept { return data_; }
    constexpr T *end() const noexcept { return size_ ? data_ + size_ : data_; }
    constexpr T &operator[](std::size_t index) const noexcept {
        assert(index < size_);
        return data_[index];
    }
    constexpr RDPSpan first(std::size_t count) const noexcept {
        assert(count <= size_);
        return {data_, count};
    }
    constexpr RDPSpan subspan(std::size_t offset, std::size_t count) const noexcept {
        assert(offset <= size_ && count <= size_ - offset);
        return {offset ? data_ + offset : data_, count};
    }

  private:
    T *data_ = nullptr;
    std::size_t size_ = 0;
};
