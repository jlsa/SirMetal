#pragma once

#import <cstdint>
#import "graphics/camera.h"
#import "core/input.h"
#import "editor/settings.h"

const char *readFile(const char *filePath);

namespace SirMetal {

    class ShaderManager;

    class TextureManager;

    struct Resources {
        TextureManager *textureManager = nullptr;
        ShaderManager *shaderManager = nullptr;
    };

    enum InteractionFlagsBits {
        InteractionNone = 0,
        InteractionViewportFocused = 1
    };

    typedef uint32_t InteractionFlags ;

    struct EditorFlags {
        InteractionFlags interaction = 0;

    };


    struct EngineContext {
        uint32_t screenWidth;
        uint32_t screenHeight;
        void *viewportTexture;
        Resources managers;
        EditorFlags flags;
        Input input;
        //for now the camera lives here until we start to have a more
        //proper rendering context;
        Camera camera;
        FPSCameraController cameraController;
        Editor::Settings settings;


    };

    bool initializeContext();
    void endFrame(EngineContext* context);

    extern EngineContext *CONTEXT;
}
