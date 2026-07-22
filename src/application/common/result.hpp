#pragma once

#include <stdexcept>
#include <utility>
#include <variant>

namespace warehouse::application {

template <typename T, typename E>
class Result {
public:
    static Result success(T value) { return Result(std::in_place_index<0>, std::move(value)); }
    static Result failure(E error) { return Result(std::in_place_index<1>, std::move(error)); }

    bool hasValue() const noexcept { return state_.index() == 0; }
    explicit operator bool() const noexcept { return hasValue(); }

    T& value() { ensureValue(); return std::get<0>(state_); }
    const T& value() const { ensureValue(); return std::get<0>(state_); }
    E& error() { ensureError(); return std::get<1>(state_); }
    const E& error() const { ensureError(); return std::get<1>(state_); }

private:
    template <std::size_t Index, typename Value>
    Result(std::in_place_index_t<Index> index, Value&& value)
        : state_(index, std::forward<Value>(value)) {}

    void ensureValue() const {
        if (!hasValue()) throw std::logic_error("result does not contain a value");
    }
    void ensureError() const {
        if (hasValue()) throw std::logic_error("result does not contain an error");
    }

    std::variant<T, E> state_;
};

template <typename E>
class Result<void, E> {
public:
    static Result success() { return Result(std::in_place_index<0>); }
    static Result failure(E error) { return Result(std::in_place_index<1>, std::move(error)); }

    bool hasValue() const noexcept { return state_.index() == 0; }
    explicit operator bool() const noexcept { return hasValue(); }
    E& error() { ensureError(); return std::get<1>(state_); }
    const E& error() const { ensureError(); return std::get<1>(state_); }

private:
    explicit Result(std::in_place_index_t<0>) : state_(std::in_place_index<0>) {}
    Result(std::in_place_index_t<1>, E error)
        : state_(std::in_place_index<1>, std::move(error)) {}
    void ensureError() const {
        if (hasValue()) throw std::logic_error("result does not contain an error");
    }

    std::variant<std::monostate, E> state_;
};

}  // namespace warehouse::application
