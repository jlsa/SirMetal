#pragma once

#include "SirMetal/core/memory/cpu/linearBufferManager.h"
#include "SirMetal/core/memory/gpu/GPUMemoryAllocator.h"
#include "SirMetal/resources/handle.h"
#include "SirMetal/engine.h"
#include <cstdint>
#include <vector>

namespace SirMetal {
    enum CONSTANT_BUFFER_FLAGS_BITS {
        CONSTANT_BUFFER_FLAG_NONE = 0,
        CONSTANT_BUFFER_FLAG_BUFFERED = 1,
    };
    typedef uint32_t CONSTANT_BUFFER_FLAGS;

    struct BindInfo
    {
        id buffer;
        uint32_t offset;
        uint32_t size;
    };

    class ConstantBufferManager {
    public:
        ConstantBufferManager()=default;
        void initialize(id device, id queue, uint32_t poolSize);
        ConstantBufferHandle allocate(EngineContext* context, uint32_t size, CONSTANT_BUFFER_FLAGS flags);
        void update(EngineContext* context,ConstantBufferHandle handle, void *data);
        void cleanup(){};
        // deleted method to avoid copy, you can still move it though
        ConstantBufferManager(const ConstantBufferManager &) = delete;
        ConstantBufferManager &operator=(const ConstantBufferManager &) = delete;
        BindInfo getBindInfo(EngineContext* context, const ConstantBufferHandle handle);
    private:
        struct PoolTracker
        {
            BufferHandle bufferHandle;
            LinearBufferManager* linearManager;
        };

        struct ConstantBufferData
        {
            BufferRange range;
            uint32_t allocIndex;
            uint32_t userRequestedSize;
            CONSTANT_BUFFER_FLAGS flags;
        };

    private:
        void allocateBufferPool();
        uint32_t findAllocator(uint32_t size);
    private:
        GPUMemoryAllocator m_allocator;
        uint32_t m_poolSize;
        std::vector<PoolTracker> m_bufferPools;
        std::vector<ConstantBufferData> m_constBuffers;

    };
}