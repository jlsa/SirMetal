#pragma once

#import <objc/objc.h>
//
#include <unordered_map>
#include <string>
#include "handle.h"
#include "SirMetal/graphics/graphicsDefines.h"

namespace SirMetal {

    class ShaderManager {
        struct ShaderMetadata {
            std::string libraryPath;
            SHADER_TYPE type;
            id vertexFn = nullptr;
            id fragFn = nullptr;
            id computeFn = nullptr;
            id library = nullptr;
        };
    public :
        LibraryHandle loadShader(const char *path);

        id getLibraryFromHandle(LibraryHandle handle);

        LibraryHandle getHandleFromName(const std::string &name) const;

        void initialize(id device) {
            m_device = device;
        };
        void cleanup(){};

        id getVertexFunction(LibraryHandle handle);
        id getFragmentFunction(LibraryHandle handle);
        id getKernelFunction(LibraryHandle handle);

      private:
        bool generateLibraryMetadata(id library,ShaderMetadata& metadata, const char* libraryPath);

    private:
        id m_device;
        std::unordered_map<uint32_t, ShaderMetadata> m_libraries;
        std::unordered_map<std::string, uint32_t> m_nameToLibraryHandle;
        uint32_t m_libraryCounter = 1;
    };
}


