
#pragma once

#include <stdint.h>

namespace SirMetal {

    enum class HANDLE_TYPE {
        NONE = 0,
        TEXTURE = 1,
        SHADER_LIBRARY = 2,
        MESH = 3,
        CONSTANT_BUFFER= 4,
        BUFFER= 4,
    };

    template<typename T>
    inline T getHandle(const uint32_t index) {
        return {(static_cast<uint32_t>(T::type) << 24)| index};
    }

    template<typename T>
    inline uint32_t getIndexFromHandle(const T h) {
        constexpr uint32_t standardIndexMask = (1 << 24) - 1;
        return h.handle & standardIndexMask;
    }

    template<typename T>
    inline HANDLE_TYPE getTypeFromHandle(const T h) {
        constexpr uint32_t standardIndexMask = (1 << 8) - 1;
        const uint32_t standardMagicNumberMask = ~standardIndexMask;
        return static_cast<HANDLE_TYPE>((h.handle & standardMagicNumberMask) >> 24);
    }

    struct TextureHandle final {
        uint32_t handle;

        [[nodiscard]] bool isHandleValid() const {
            return handle != 0;
        }
        static const HANDLE_TYPE type = HANDLE_TYPE::TEXTURE;
    };

    struct LibraryHandle final {
        uint32_t handle;

        [[nodiscard]] bool isHandleValid() const {
            return handle != 0;
        }
        static const HANDLE_TYPE type = HANDLE_TYPE::SHADER_LIBRARY;
    };
    struct MeshHandle final {
        uint32_t handle;

        [[nodiscard]] bool isHandleValid() const {
            return handle != 0;
        }
        static const HANDLE_TYPE type = HANDLE_TYPE::MESH;
    };
    struct ConstantBufferHandle final {
        uint32_t handle;

        [[nodiscard]] bool isHandleValid() const {
            return handle != 0;
        }
        static const HANDLE_TYPE type = HANDLE_TYPE::CONSTANT_BUFFER;
    };
    struct BufferHandle final {
        uint32_t handle;

        [[nodiscard]] bool isHandleValid() const {
            return handle != 0;
        }
        static const HANDLE_TYPE type = HANDLE_TYPE::BUFFER;
    };
}