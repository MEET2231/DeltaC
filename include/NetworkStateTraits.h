#pragma once

#include "BitWriter.h"
#include "BitReader.h"
#include <type_traits>

namespace DeltaCompression {

// C++17 SFINAE trait to validate that user-defined struct T fulfills
// the Delta Serialization contract required by the engine.
template <typename T, typename = void>
struct is_delta_serializable : std::false_type {};

template <typename T>
struct is_delta_serializable<T, std::void_t<
    decltype(std::declval<const T&>().SerializeDelta(std::declval<BitWriter&>(), std::declval<const T&>())),
    decltype(std::declval<T&>().DeserializeDelta(std::declval<BitReader&>(), std::declval<const T&>()))
>> : std::true_type {};

template <typename T>
inline constexpr bool is_delta_serializable_v = is_delta_serializable<T>::value;

} // namespace DeltaCompression
