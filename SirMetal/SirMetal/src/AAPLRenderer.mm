#import <Metal/Metal.h>
#import "MetalKit/MetalKit.h"

#import "AAPLRenderer.h"
#include "engineContext.h"
#include "shaderManager.h"
#include "resources/textureManager.h"
#import "MBEMathUtilities.h"
#import "vendors/imgui/imgui.h"
#import "vendors/imgui/imgui_impl_metal.h"
#import "imgui_impl_osx.h"
#import "imgui_internal.h"
#import "editorUI.h"
#import "log.h"

typedef struct {
    vector_float4 position;
    vector_float4 color;
} MBEVertex;
static const NSInteger MBEInFlightBufferCount = 3;

typedef uint16_t MBEIndex;
const MTLIndexType MBEIndexType = MTLIndexTypeUInt16;

typedef struct {
    matrix_float4x4 modelViewProjectionMatrix;
} MBEUniforms;

static inline uint64_t AlignUp(uint64_t n, uint32_t alignment) {
    return ((n + alignment - 1) / alignment) * alignment;
}

static const uint32_t MBEBufferAlignment = 256;


//temporary until i figure out what to do with this
static SirMetal::EditorUI editorUI = SirMetal::EditorUI();
static bool shouldResizeOffScreen = false;

@interface AAPLRenderer ()
@property(strong) id <MTLRenderPipelineState> renderPipelineState;
@property(strong) id <MTLDepthStencilState> depthStencilState;
@property(strong) id <MTLTexture> depthTexture;
@property(strong) id <MTLTexture> depthTextureGUI;
@property(strong) id <MTLTexture> offScreenTexture;
@property(nonatomic, strong) id <MTLBuffer> vertexBuffer;
@property(strong) id <MTLBuffer> indexBuffer;
@property(strong) id <MTLBuffer> uniformBuffer;
//@property(assign) ImVec2 viewportSize;
//@property(assign) ImVec2 viewportPanelSize;
@property(strong) dispatch_semaphore_t displaySemaphore;
@property(assign) NSInteger bufferIndex;
@property(assign) float rotationX, rotationY, time;
//@property(assign) bool shouldResizeOffScreen;

@end

/*
@property (strong) id<MTLDevice> device;
@property (strong) id<MTLBuffer> vertexBuffer;
@property (strong) id<MTLBuffer> indexBuffer;
@property (strong) id<MTLBuffer> uniformBuffer;
@property (strong) id<MTLCommandQueue> commandQueue;
@property (strong) id<MTLRenderPipelineState> renderPipelineState;
@property (strong) id<MTLDepthStencilState> depthStencilState;
@property (strong) dispatch_semaphore_t displaySemaphore;
@property (assign) NSInteger bufferIndex;
@property (assign) float rotationX, rotationY, time;
*/
// Main class performing the rendering
@implementation AAPLRenderer {
    id <MTLDevice> _device;

    // The command queue used to pass commands to the device.
    id <MTLCommandQueue> _commandQueue;
}

- (void)logGPUInformation:(id <MTLDevice>)device {
    NSLog(@"Device name: %@", device.name);
    NSLog(@"\t\tis low power: %@", device.isLowPower ? @"YES" : @"NO");
    NSLog(@"\t\tis removable: %@", device.isRemovable ? @"YES" : @"NO");
    NSLog(@"\t\tsupport macOS GPU family 1: %@", [device supportsFeatureSet:MTLFeatureSet::MTLFeatureSet_macOS_GPUFamily1_v1] ? @"YES" : @"NO");
    NSLog(@"\t\tsupport macOS GPU family 2: %@", [device supportsFeatureSet:MTLFeatureSet::MTLFeatureSet_macOS_GPUFamily2_v1] ? @"YES" : @"NO");
}


- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)mtkView {
    self = [super init];
    if (self) {
        _device = mtkView.device;
        _displaySemaphore = dispatch_semaphore_create(MBEInFlightBufferCount);


        // Create the command queue
        _commandQueue = [_device newCommandQueue];
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplMetal_Init(_device);


    NSLog(@"Initializing Sir Metal: v0.0.1");
    [self logGPUInformation:_device];

    mtkView.paused = NO;

    int w = mtkView.drawableSize.width;
    int h = mtkView.drawableSize.height;


    //create the pipeline
    [self makePipeline];
    [self makeBuffers];
    //TODO probably grab the start viewport size from the context
    [self createOffscreenTexture:256 :256];


    MTLTextureDescriptor *descriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:w height:h mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.pixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    self.depthTextureGUI = [_device newTextureWithDescriptor:descriptor];
    self.depthTextureGUI.label = @"DepthStencilGUI";

    return self;
}

- (void)makePipeline {

    char buffer[256];
    sprintf(buffer, "%s/%s", SirMetal::CONTEXT->projectPath, "/shaders/Shaders.metal");
    SirMetal::LibraryHandle lh = SirMetal::CONTEXT->managers.shaderManager->loadShader(buffer, _device);
    id <MTLLibrary> rawLib = SirMetal::CONTEXT->managers.shaderManager->getLibraryFromHandle(lh);

    MTLRenderPipelineDescriptor *pipelineDescriptor = [MTLRenderPipelineDescriptor new];
    pipelineDescriptor.vertexFunction = [rawLib newFunctionWithName:@"vertex_project"];
    pipelineDescriptor.fragmentFunction = [rawLib newFunctionWithName:@"fragment_flatcolor"];
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    //pipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    MTLDepthStencilDescriptor *depthStencilDescriptor = [MTLDepthStencilDescriptor new];
    depthStencilDescriptor.depthCompareFunction = MTLCompareFunctionLess;
    depthStencilDescriptor.depthWriteEnabled = YES;
    self.depthStencilState = [_device newDepthStencilStateWithDescriptor:depthStencilDescriptor];

    NSError *error = nil;
    self.renderPipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                                                       error:&error];

    if (!self.renderPipelineState) {
        NSLog(@"Error occurred when creating render pipeline state: %@", error);
    }
}

- (void)makeBuffers {
    static const MBEVertex vertices[] =
            {
                    {.position = {-1, 1, 1, 1}, .color = {0, 1, 1, 1}},
                    {.position = {-1, -1, 1, 1}, .color = {0, 0, 1, 1}},
                    {.position = {1, -1, 1, 1}, .color = {1, 0, 1, 1}},
                    {.position = {1, 1, 1, 1}, .color = {1, 1, 1, 1}},
                    {.position = {-1, 1, -1, 1}, .color = {0, 1, 0, 1}},
                    {.position = {-1, -1, -1, 1}, .color = {0, 0, 0, 1}},
                    {.position = {1, -1, -1, 1}, .color = {1, 0, 0, 1}},
                    {.position = {1, 1, -1, 1}, .color = {1, 1, 0, 1}}
            };

    static const MBEIndex indices[] =
            {
                    3, 2, 6, 6, 7, 3,
                    4, 5, 1, 1, 0, 4,
                    4, 0, 3, 3, 7, 4,
                    1, 5, 6, 6, 2, 1,
                    0, 1, 2, 2, 3, 0,
                    7, 6, 5, 5, 4, 7
            };

    _vertexBuffer = [_device newBufferWithBytes:vertices
                                         length:sizeof(vertices)
                                        options:MTLResourceOptionCPUCacheModeDefault];
    [_vertexBuffer setLabel:@"Vertices"];

    _indexBuffer = [_device newBufferWithBytes:indices
                                        length:sizeof(indices)
                                       options:MTLResourceOptionCPUCacheModeDefault];
    [_indexBuffer setLabel:@"Indices"];

    _uniformBuffer = [_device newBufferWithLength:AlignUp(sizeof(MBEUniforms), MBEBufferAlignment) * MBEInFlightBufferCount
                                          options:MTLResourceOptionCPUCacheModeDefault];
    [_uniformBuffer setLabel:@"Uniforms"];
}

- (void)updateUniformsForView:(float)screenWidth :(float)screenHeight {

    //float duration = 0.01;
    float duration = 0.00;
    self.time += duration;
    self.rotationX += duration * (M_PI / 2);
    self.rotationY += duration * (M_PI / 3);
    float scaleFactor = sinf(5 * self.time) * 0.25 + 1;
    const vector_float3 xAxis = {1, 0, 0};
    const vector_float3 yAxis = {0, 1, 0};
    const matrix_float4x4 xRot = matrix_float4x4_rotation(xAxis, self.rotationX);
    const matrix_float4x4 yRot = matrix_float4x4_rotation(yAxis, self.rotationY);
    const matrix_float4x4 scale = matrix_float4x4_uniform_scale(scaleFactor);
    const matrix_float4x4 modelMatrix = matrix_multiply(matrix_multiply(xRot, yRot), scale);

    const vector_float3 cameraTranslation = {0, 0, -5};
    const matrix_float4x4 viewMatrix = matrix_float4x4_translation(cameraTranslation);

    const float aspect = screenWidth / screenHeight;
    const float fov = (2 * M_PI) / 5;
    const float near = 1;
    const float far = 100;
    const matrix_float4x4 projectionMatrix = matrix_float4x4_perspective(aspect, fov, near, far);

    MBEUniforms uniforms;
    uniforms.modelViewProjectionMatrix = matrix_multiply(projectionMatrix, matrix_multiply(viewMatrix, modelMatrix));

    const NSUInteger uniformBufferOffset = AlignUp(sizeof(MBEUniforms), MBEBufferAlignment) * self.bufferIndex;
    memcpy((char *) ([self.uniformBuffer contents]) + uniformBufferOffset, &uniforms, sizeof(uniforms));
}

- (void)createOffscreenTexture:(int)width :(int)height {

    SirMetal::TextureManager *textureManager = SirMetal::CONTEXT->managers.textureManager;

    SirMetal::AllocTextureRequest request{
            static_cast<uint32_t>(width), static_cast<uint32_t>(height),
            1, MTLTextureType2D, MTLPixelFormatBGRA8Unorm,
            MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead, MTLStorageModePrivate, 1, "offscreenTexture"
    };
    self.viewportHandle = textureManager->allocate(_device, request);
    self.offScreenTexture = textureManager->getNativeFromHandle(self.viewportHandle);
    SirMetal::CONTEXT->viewportTexture = self.offScreenTexture;


    SirMetal::AllocTextureRequest requestDepth{
            static_cast<uint32_t>(width), static_cast<uint32_t>(height),
            1, MTLTextureType2D, MTLPixelFormatDepth32Float_Stencil8,
            MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead, MTLStorageModePrivate, 1, "depthTexture"
    };
    self.depthHandle = textureManager->allocate(_device, requestDepth);
    self.depthTexture = textureManager->getNativeFromHandle(self.depthHandle);
}

- (void)renderUI:(MTKView *)view :(MTLRenderPassDescriptor *)passDescriptor :(id <MTLCommandBuffer>)commandBuffer
        :(id <MTLRenderCommandEncoder>)renderPass {
    // Start the Dear ImGui frame
    ImGui_ImplOSX_NewFrame(view);
    ImGui_ImplMetal_NewFrame(passDescriptor);
    ImGui::NewFrame();


    //[self showImguiContent];
    shouldResizeOffScreen = editorUI.show(SirMetal::CONTEXT->screenWidth, SirMetal::CONTEXT->screenHeight);
    // Rendering
    ImGui::Render();
    ImDrawData *drawData = ImGui::GetDrawData();
    ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer, renderPass);

}

/// Called whenever the view needs to render a frame.
- (void)drawInMTKView:(nonnull MTKView *)view {

    //dispatch_semaphore_wait(self.displaySemaphore, DISPATCH_TIME_FOREVER);
    ImVec2 viewportSize = editorUI.getViewportSize();
    if (shouldResizeOffScreen) {
        // [self createOffscreenTexture:(int) viewportSize.x :(int) viewportSize.y];
        SirMetal::TextureManager *texManager = SirMetal::CONTEXT->managers.textureManager;
        bool viewportResult = texManager->resizeTexture(_device, self.viewportHandle, viewportSize.x, viewportSize.y);
        bool depthResult = texManager->resizeTexture(_device, self.depthHandle, viewportSize.x, viewportSize.y);
        if (viewportResult | depthResult) {

        }
        SIR_CORE_FATAL("Could not resize viewport color or depth buffer");
        self.offScreenTexture = texManager->getNativeFromHandle(self.viewportHandle);
        self.depthTexture = texManager->getNativeFromHandle(self.depthHandle);
        SirMetal::CONTEXT->viewportTexture = self.offScreenTexture;
        shouldResizeOffScreen = false;
    }


    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize.x = static_cast<float>(view.bounds.size.width);
    io.DisplaySize.y = static_cast<float>(view.bounds.size.height);

    CGFloat framebufferScale = view.window.screen.backingScaleFactor ?: NSScreen.mainScreen.backingScaleFactor;
    io.DisplayFramebufferScale = ImVec2(framebufferScale, framebufferScale);
    io.DeltaTime = 1 / float(view.preferredFramesPerSecond ?: 60);

    view.clearColor = MTLClearColorMake(0.95, 0.95, 0.95, 1);

    float w = view.drawableSize.width;
    float h = view.drawableSize.height;

    bool isViewport = (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) > 0;
    //NSLog(@"%.1fx%.1f",w, h);
    [self updateUniformsForView:(isViewport ? viewportSize.x : w) :(isViewport ? viewportSize.y : h)];

    id <MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor *passDescriptor = [view currentRenderPassDescriptor];
    //passDescriptor.depthAttachment


    passDescriptor.renderTargetWidth = isViewport ? viewportSize.x : w;
    passDescriptor.renderTargetHeight = isViewport ? viewportSize.y : h;
    MTLRenderPassColorAttachmentDescriptor *colorAttachment = passDescriptor.colorAttachments[0];
    colorAttachment.texture = isViewport ? self.offScreenTexture : view.currentDrawable.texture;
    colorAttachment.clearColor = MTLClearColorMake(0.8, 0.8, 0.8, 1.0);
    colorAttachment.storeAction = MTLStoreActionStore;
    colorAttachment.loadAction = MTLLoadActionClear;

    MTLRenderPassDepthAttachmentDescriptor *depthAttachment = passDescriptor.depthAttachment;
    depthAttachment.texture = isViewport ? self.depthTexture : self.depthTextureGUI;
    depthAttachment.clearDepth = 1.0;
    depthAttachment.storeAction = MTLStoreActionDontCare;
    depthAttachment.loadAction = MTLLoadActionClear;

    /*
    MTLRenderPassStencilAttachmentDescriptor *stencilAttachment = _metalRenderPassDesc.stencilAttachment;
    stencilAttachment.texture = depthAttachment.texture;
    stencilAttachment.storeAction = MTLStoreActionDontCare;
    stencilAttachment.loadAction = desc.clear ? MTLLoadActionClear : MTLLoadActionLoad;
    */




    id <MTLRenderCommandEncoder> renderPass = [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
    [renderPass setRenderPipelineState:self.renderPipelineState];
    [renderPass setDepthStencilState:self.depthStencilState];
    [renderPass setFrontFacingWinding:MTLWindingCounterClockwise];
    [renderPass setCullMode:MTLCullModeBack];


    const NSUInteger uniformBufferOffset = AlignUp(sizeof(MBEUniforms), MBEBufferAlignment) * self.bufferIndex;

    [renderPass setVertexBuffer:self.vertexBuffer offset:0 atIndex:0];
    [renderPass setVertexBuffer:self.uniformBuffer offset:uniformBufferOffset atIndex:1];

    [renderPass drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                           indexCount:[self.indexBuffer length] / sizeof(MBEIndex)
                            indexType:MBEIndexType
                          indexBuffer:self.indexBuffer
                    indexBufferOffset:0];

    [renderPass endEncoding];


    if (isViewport) {

        MTLRenderPassDescriptor *uiPassDescriptor = [[MTLRenderPassDescriptor alloc] init];
        MTLRenderPassColorAttachmentDescriptor *uiColorAttachment = uiPassDescriptor.colorAttachments[0];
        uiColorAttachment.texture = view.currentDrawable.texture;
        uiColorAttachment.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        uiColorAttachment.storeAction = MTLStoreActionStore;
        uiColorAttachment.loadAction = MTLLoadActionClear;

        MTLRenderPassDepthAttachmentDescriptor *uiDepthAttachment = uiPassDescriptor.depthAttachment;
        uiDepthAttachment.texture = self.depthTextureGUI;
        uiDepthAttachment.clearDepth = 1.0;
        uiDepthAttachment.storeAction = MTLStoreActionDontCare;
        uiDepthAttachment.loadAction = MTLLoadActionClear;

        uiPassDescriptor.renderTargetWidth = w;
        uiPassDescriptor.renderTargetHeight = h;

        id <MTLRenderCommandEncoder> uiPass = [commandBuffer renderCommandEncoderWithDescriptor:uiPassDescriptor];


        [self renderUI:view :passDescriptor :commandBuffer :uiPass];


        [uiPass endEncoding];
    }

    [commandBuffer presentDrawable:view.currentDrawable];

    //[commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> commandBuffer) {
    //    self.bufferIndex = (self.bufferIndex + 1) % MBEInFlightBufferCount;
    //    dispatch_semaphore_signal(self.displaySemaphore);
    //}];

    [commandBuffer commit];


}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size {

    //here we divide by the scale factor so our engine will work with the
    //real sizes if needed and not scaled up views
    CGFloat scaling = view.window.screen.backingScaleFactor;
    SirMetal::CONTEXT->screenWidth = static_cast<float>(size.width / scaling);
    SirMetal::CONTEXT->screenHeight = static_cast<float>(size.height / scaling);
}

@end

