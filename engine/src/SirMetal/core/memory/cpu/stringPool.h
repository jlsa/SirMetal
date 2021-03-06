#pragma once

#include <type_traits>
#include "SirMetal/core/memory/cpu/stackAllocator.h"
#include "SirMetal/core/memory/cpu/threeSizesPool.h"

namespace SirMetal {

enum STRING_MANIPULATION_FLAGS {
  NO_OPERATION = 0,
  FREE_FIRST_AFTER_OPERATION = 1 << 1,
  FREE_SECOND_AFTER_OPERATION = 1 << 2,
  FREE_JOINER_AFTER_OPERATION = 1 << 3
};
// NOTE: this class does not handle a string starting with leading spaces
class  StringPool final {
 public:
 public:
  explicit StringPool(const uint32_t sizeInByte) : m_pool(sizeInByte) {
    m_stackAllocator.initialize(sizeInByte);
  };
  // deleted copy constructors and assignment operator
  StringPool(const StringPool&) = delete;
  StringPool& operator=(const StringPool&) = delete;

  // allocations and free
  const char* allocatePersistent(const char* string);
  const wchar_t* allocatePersistent(const wchar_t* string);
  const char* allocateFrame(const char* string);
  const wchar_t* allocateFrame(const wchar_t* string);

  inline void free(const char* string) { m_pool.free((void*)string); }
  inline void free(const wchar_t* string) { m_pool.free((void*)string); }
  inline void resetFrameMemory() { m_stackAllocator.reset(); }

  //file loading
  const char* loadFilePersistent(const char* path, uint32_t& readFileSize);
  const char* loadFileFrame(const char* path, uint32_t& readFileSize);

  // string manipulation
  const char* concatenatePersistent(const char* first, const char* second,
                                    const char* joiner = nullptr,
                                    const uint8_t flags = 0);
  // overloads with conversions
  template <typename FIRST, typename SECOND, typename JOINER>
  const char* concatenatePersistent(const FIRST* first, const SECOND* second,
                                    const JOINER* joiner = nullptr,
                                    const uint8_t flags = 0) {
    static_assert(std::is_same<wchar_t, FIRST>::value ||
                  std::is_same<char, FIRST>::value);
    static_assert(std::is_same<wchar_t, SECOND>::value ||
                  std::is_same<char, SECOND>::value);
    static_assert(std::is_same<wchar_t, JOINER>::value ||
                  std::is_same<char, JOINER>::value);

    const char* firstFix;
    const char* secondFix;
    const char* joinerFix;
    uint8_t newFlags = flags;
    // convert first type if needed
    if constexpr (std::is_same<FIRST, wchar_t>::value) {
      firstFix = convert(first, flags);
      newFlags = newFlags | FREE_FIRST_AFTER_OPERATION;
    } else {
      firstFix = first;
    }

    // convert second type if needed
    if constexpr (std::is_same<SECOND, wchar_t>::value) {
      secondFix = convert(second, flags);
      newFlags = newFlags | FREE_SECOND_AFTER_OPERATION;
    } else {
      secondFix = second;
    }
    // convert joiner type if needed
    if constexpr (std::is_same<JOINER, wchar_t>::value) {
      joinerFix = convert(joiner, flags);
      newFlags = newFlags | FREE_JOINER_AFTER_OPERATION;
    } else {
      joinerFix = joiner;
    }

    return concatenatePersistent(firstFix, secondFix, joinerFix, newFlags);
  }

  template <typename FIRST, typename SECOND, typename JOINER>
  const wchar_t* concatenatePersistentWide(const FIRST* first,
                                           const SECOND* second,
                                           const JOINER* joiner = nullptr,
                                           const uint8_t flags = 0) {
    static_assert(std::is_same<wchar_t, FIRST>::value ||
                  std::is_same<char, FIRST>::value);
    static_assert(std::is_same<wchar_t, SECOND>::value ||
                  std::is_same<char, SECOND>::value);
    static_assert(std::is_same<wchar_t, JOINER>::value ||
                  std::is_same<char, JOINER>::value);

    const wchar_t* firstFix;
    const wchar_t* secondFix;
    const wchar_t* joinerFix;
    uint8_t newFlags = flags;
    // convert first type if needed
    if constexpr (std::is_same<FIRST, char>::value) {
      firstFix = convertWide(first, flags);
      newFlags = newFlags | FREE_FIRST_AFTER_OPERATION;
    } else {
      firstFix = first;
    }

    // convert second type if needed
    if constexpr (std::is_same<SECOND, char>::value) {
      secondFix = convertWide(second, flags);
      newFlags = newFlags | FREE_SECOND_AFTER_OPERATION;
    } else {
      secondFix = second;
    }
    // convert joiner type if needed
    if constexpr (std::is_same<JOINER, char>::value) {
      joinerFix = convertWide(joiner, flags);
      newFlags = newFlags | FREE_JOINER_AFTER_OPERATION;
    } else {
      joinerFix = joiner;
    }

    return concatenatePersistentWide(firstFix, secondFix, joinerFix, newFlags);
  }

  const wchar_t* concatenatePersistentWide(const wchar_t* first,
                                           const wchar_t* second,
                                           const wchar_t* joiner = nullptr,
                                           const uint8_t flags = 0);

  const char* concatenateFrame(const char* first, const char* second,
                               const char* joiner = nullptr);

  template <typename FIRST, typename SECOND, typename JOINER>
  const char* concatenateFrame(const FIRST* first, const SECOND* second,
                               const JOINER* joiner = nullptr) {
    static_assert(std::is_same<wchar_t, FIRST>::value ||
                  std::is_same<char, FIRST>::value);
    static_assert(std::is_same<wchar_t, SECOND>::value ||
                  std::is_same<char, SECOND>::value);
    static_assert(std::is_same<wchar_t, JOINER>::value ||
                  std::is_same<char, JOINER>::value);

    const char* firstFix;
    const char* secondFix;
    const char* joinerFix;
    // convert first type if needed
    if constexpr (std::is_same<FIRST, wchar_t>::value) {
      firstFix = convertFrame(first);
    } else {
      firstFix = first;
    }

    // convert second type if needed
    if constexpr (std::is_same<SECOND, wchar_t>::value) {
      secondFix = convertFrame(second);
    } else {
      secondFix = second;
    }
    // convert joiner type if needed
    if constexpr (std::is_same<JOINER, wchar_t>::value) {
      joinerFix = convertFrame(joiner);
    } else {
      joinerFix = joiner;
    }

    return concatenateFrame(firstFix, secondFix, joinerFix);
  }
  template <typename FIRST, typename SECOND, typename JOINER>
  const wchar_t* concatenateFrameWide(const FIRST* first,
                                           const SECOND* second,
                                           const JOINER* joiner = nullptr
                                           ) {
    static_assert(std::is_same<wchar_t, FIRST>::value ||
                  std::is_same<char, FIRST>::value);
    static_assert(std::is_same<wchar_t, SECOND>::value ||
                  std::is_same<char, SECOND>::value);
    static_assert(std::is_same<wchar_t, JOINER>::value ||
                  std::is_same<char, JOINER>::value);

    const wchar_t* firstFix;
    const wchar_t* secondFix;
    const wchar_t* joinerFix;
    // convert first type if needed
    if constexpr (std::is_same<FIRST, char>::value) {
      firstFix = convertFrameWide(first);
    } else {
      firstFix = first;
    }

    // convert second type if needed
    if constexpr (std::is_same<SECOND, char>::value) {
      secondFix = convertFrameWide(second);
    } else {
      secondFix = second;
    }
    // convert joiner type if needed
    if constexpr (std::is_same<JOINER, char>::value) {
      joinerFix = convertFrameWide(joiner );
    } else {
      joinerFix = joiner;
    }

    return concatenateFrameWide(firstFix, secondFix, joinerFix);
  }
  const wchar_t* concatenateFrameWide(const wchar_t* first,
                                           const wchar_t* second,
                                           const wchar_t* joiner = nullptr
                                           ); 

  const char* convert(const wchar_t* string, const uint8_t flags = 0);
  const char* convertFrame(const wchar_t* string);
  const wchar_t* convertWide(const char* string, const uint8_t flags = 0);
  const wchar_t* convertFrameWide(const char* string);

 private:
  enum class STRING_TYPE { CHAR = 1, WCHAR = 2 };

 private:
  ThreeSizesPool m_pool;
  StackAllocator m_stackAllocator;
};

}  // namespace SirMetal
