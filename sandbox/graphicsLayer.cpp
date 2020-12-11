#include "graphicsLayer.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SirMetal/MBEMathUtilities.h>
#include <SirMetal/resources/textureManager.h>
#include <simd/simd.h>

#include "SirMetal/application/window.h"
#include "SirMetal/core/input.h"
#include "SirMetal/engine.h"
#include "SirMetal/graphics/PSOGenerator.h"
#include "SirMetal/graphics/constantBufferManager.h"
#include "SirMetal/graphics/debug/debugRenderer.h"
#include "SirMetal/graphics/debug/imgui/imgui.h"
#include "SirMetal/graphics/debug/imguiRenderer.h"
#include "SirMetal/graphics/materialManager.h"
#include "SirMetal/graphics/renderingContext.h"
#include "SirMetal/resources/meshes/meshManager.h"
#include "SirMetal/resources/shaderManager.h"

#import <MetalPerformanceShaders/MetalPerformanceShaders.h>

static const size_t rayStride = 48;
static const size_t intersectionStride =
    sizeof(MPSIntersectionDistancePrimitiveIndexCoordinates);
constexpr int kMaxInflightBuffers = 3;

namespace Sandbox {
void GraphicsLayer::onAttach(SirMetal::EngineContext *context) {

  m_engine = context;
  m_gpuAllocator.initialize(m_engine->m_renderingContext->getDevice(),
                            m_engine->m_renderingContext->getQueue());

  // initializing the camera to the identity
  m_camera.viewMatrix = matrix_float4x4_translation(vector_float3{0, 0, 0});
  m_camera.fov = M_PI / 4;
  m_camera.nearPlane = 0.01;
  m_camera.farPlane = 60;
  m_cameraController.setCamera(&m_camera);
  m_cameraController.setPosition(3, 5, 15);
  m_camConfig = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2, 0.008};

  m_uniformHandle = m_engine->m_constantBufferManager->allocate(
      m_engine, sizeof(SirMetal::Camera),
      SirMetal::CONSTANT_BUFFER_FLAGS_BITS::CONSTANT_BUFFER_FLAG_BUFFERED);
  m_lightHandle = m_engine->m_constantBufferManager->allocate(
      m_engine, sizeof(DirLight),
      SirMetal::CONSTANT_BUFFER_FLAGS_BITS::CONSTANT_BUFFER_FLAG_NONE);

  updateLightData();

  const std::string base = m_engine->m_config.m_dataSourcePath + "/sandbox";

  const char *names[1] = {"/rt.obj"};
  for (int i = 0; i < 1; ++i) {
    m_meshes[i] = m_engine->m_meshManager->loadMesh(base + names[i]);
  }

  id<MTLDevice> device = m_engine->m_renderingContext->getDevice();

  SirMetal::AllocTextureRequest request{
      m_engine->m_config.m_windowConfig.m_width,
      m_engine->m_config.m_windowConfig.m_height,
      1,
      MTLTextureType2D,
      MTLPixelFormatRGBA16Float,
      MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
          MTLTextureUsageShaderWrite,
      MTLStorageModePrivate,
      1,
      "colorRayTracing"};
  m_color = m_engine->m_textureManager->allocate(device, request);

  m_shaderHandle =
      m_engine->m_shaderManager->loadShader((base + "/Shaders.metal").c_str());
  m_rtGenShaderHandle =
      m_engine->m_shaderManager->loadShader((base + "/rtGen.metal").c_str());

  // let us start building the raytracing stuff
  // Create a raytracer for our Metal device
  m_intersector = [[MPSRayIntersector alloc] initWithDevice:device];

  m_intersector.rayDataType = MPSRayDataTypeOriginDirection;
  m_intersector.rayStride = rayStride;
  m_intersector.rayMaskOptions = MPSRayMaskOptionPrimitive;

  // Create an acceleration structure from our vertex position data
  m_accelerationStructure =
      [[MPSTriangleAccelerationStructure alloc] initWithDevice:device];

  const SirMetal::MeshData *meshData =
      m_engine->m_meshManager->getMeshData(m_meshes[0]);
  m_accelerationStructure.vertexBuffer = meshData->vertexBuffer;
  m_accelerationStructure.indexBuffer = meshData->indexBuffer;
  m_accelerationStructure.maskBuffer = nil;
  m_accelerationStructure.triangleCount = meshData->primitivesCount / 3;
  m_accelerationStructure.vertexStride = sizeof(float) * 4;

  [m_accelerationStructure rebuild];

  uint32_t w = m_engine->m_config.m_windowConfig.m_width;
  uint32_t h = m_engine->m_config.m_windowConfig.m_height;
  uint32_t pixelCount = w * h;
  uint32_t raysBufferSize = pixelCount * rayStride;
  m_rayBuffer = m_gpuAllocator.allocate(
      raysBufferSize, "rayBuffers",
      SirMetal::BUFFER_FLAGS_BITS::BUFFER_FLAG_GPU_ONLY);

  SirMetal::graphics::initImgui(m_engine);
  frameBoundarySemaphore = dispatch_semaphore_create(kMaxInflightBuffers);



  // Create compute pipelines will will execute code on the GPU
  MTLComputePipelineDescriptor *computeDescriptor = [[MTLComputePipelineDescriptor alloc] init];

  // Set to YES to allow compiler to make certain optimizations
  computeDescriptor.threadGroupSizeIsMultipleOfThreadExecutionWidth = YES;

  // Generates rays according to view/projection matrices
  computeDescriptor.computeFunction = m_engine->m_shaderManager->getKernelFunction(m_rtGenShaderHandle);

  //ray pipeline
  NSError *error = NULL;
  rayPipeline = [device newComputePipelineStateWithDescriptor:computeDescriptor
  options:0
  reflection:nil
  error:&error];

  if (!rayPipeline)
    NSLog(@"Failed to create pipeline state: %@", error);
}

void GraphicsLayer::onDetach() {}

void GraphicsLayer::updateUniformsForView(float screenWidth,
                                          float screenHeight) {

  SirMetal::Input *input = m_engine->m_inputManager;

  if (!ImGui::GetIO().WantCaptureMouse) {
    m_cameraController.update(m_camConfig, input);
  }

  m_cameraController.updateProjection(screenWidth, screenHeight);
  m_engine->m_constantBufferManager->update(m_engine, m_uniformHandle,
                                            &m_camera);
}

void GraphicsLayer::onUpdate() {

  // NOTE: very temp ESC handy to close the game
  if (m_engine->m_inputManager->isKeyDown(SDL_SCANCODE_ESCAPE)) {
    SDL_Event sdlevent{};
    sdlevent.type = SDL_QUIT;
    SDL_PushEvent(&sdlevent);
  }

  CAMetalLayer *swapchain = m_engine->m_renderingContext->getSwapchain();
  id<MTLCommandQueue> queue = m_engine->m_renderingContext->getQueue();

  // waiting for resource
  dispatch_semaphore_wait(frameBoundarySemaphore, DISPATCH_TIME_FOREVER);

  id<CAMetalDrawable> surface = [swapchain nextDrawable];
  id<MTLTexture> texture = surface.texture;

  float w = texture.width;
  float h = texture.height;
  updateUniformsForView(w, h);
  updateLightData();

  // render
  SirMetal::graphics::DrawTracker tracker{};
  tracker.renderTargets[0] = texture;
  tracker.depthTarget = nullptr;

  SirMetal::PSOCache cache =
      SirMetal::getPSO(m_engine, tracker, SirMetal::Material{"Shaders", false});

  MTLRenderPassDescriptor *passDescriptor =
  [MTLRenderPassDescriptor renderPassDescriptor];

  passDescriptor.colorAttachments[0].texture = texture;
  passDescriptor.colorAttachments[0].clearColor = {0.2, 0.2, 0.2, 1.0};
  passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
  passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;

  id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
  id<MTLRenderCommandEncoder> commandEncoder =
  [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];

  /*
  // We will launch a rectangular grid of threads on the GPU to generate the
  // rays. Threads are launched in groups called "threadgroups". We need to
  // align the number of threads to be a multiple of the threadgroup size. We
  // indicated when compiling the pipeline that the threadgroup size would be a
  // multiple of the thread execution width (SIMD group size) which is typically
  // 32 or 64 so 8x8 is a safe threadgroup size which should be small to be
  // supported on most devices. A more advanced application would choose the
  // threadgroup size dynamically.
  MTLSize threadsPerThreadgroup = MTLSizeMake(8, 8, 1);
  MTLSize threadgroups = MTLSizeMake(
      (w + threadsPerThreadgroup.width - 1) / threadsPerThreadgroup.width,
      (h + threadsPerThreadgroup.height - 1) / threadsPerThreadgroup.height, 1);

  // First, we will generate rays on the GPU. We create a compute command
  // encoder which will be used to add commands to the command buffer.
  id<MTLComputeCommandEncoder> computeEncoder =
      [commandBuffer computeCommandEncoder];

  // Bind buffers needed by the compute pipeline
  [computeEncoder setBuffer:_uniformBuffer
                     offset:_uniformBufferOffset
                    atIndex:0];
  [computeEncoder setBuffer:_rayBuffer offset:0 atIndex:1];

  // Bind the ray generation compute pipeline
  [computeEncoder setComputePipelineState:_rayPipeline];

  // Launch threads
  [computeEncoder dispatchThreadgroups:threadgroups
                 threadsPerThreadgroup:threadsPerThreadgroup];
  */






  /*


  [commandEncoder setRenderPipelineState:cache.color];
  [commandEncoder setDepthStencilState:cache.depth];
  [commandEncoder setFrontFacingWinding:MTLWindingCounterClockwise];
  [commandEncoder setCullMode:MTLCullModeBack];

  SirMetal::BindInfo info =
      m_engine->m_constantBufferManager->getBindInfo(m_engine,
  m_uniformHandle); [commandEncoder setVertexBuffer:info.buffer
  offset:info.offset atIndex:4]; info =
      m_engine->m_constantBufferManager->getBindInfo(m_engine,
  m_lightHandle); [commandEncoder setFragmentBuffer:info.buffer
  offset:info.offset atIndex:5];

  for (auto &mesh : m_meshes) {
    const SirMetal::MeshData *meshData =
        m_engine->m_meshManager->getMeshData(mesh);

    [commandEncoder setVertexBuffer:meshData->vertexBuffer
                             offset:meshData->ranges[0].m_offset
                            atIndex:0];
    [commandEncoder setVertexBuffer:meshData->vertexBuffer
                             offset:meshData->ranges[1].m_offset
                            atIndex:1];
    [commandEncoder setVertexBuffer:meshData->vertexBuffer
                             offset:meshData->ranges[2].m_offset
                            atIndex:2];
    [commandEncoder setVertexBuffer:meshData->vertexBuffer
                             offset:meshData->ranges[3].m_offset
                            atIndex:3];
    [commandEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                               indexCount:meshData->primitivesCount
                                indexType:MTLIndexTypeUInt32
                              indexBuffer:meshData->indexBuffer
                        indexBufferOffset:0];
  }
  // render debug
  m_engine->m_debugRenderer->newFrame();
  float data[6]{0, 0, 0, 0, 100, 0};
  m_engine->m_debugRenderer->drawLines(data, sizeof(float) * 6,
                                       vector_float4{1, 0, 0, 1});
  m_engine->m_debugRenderer->render(m_engine, commandEncoder, tracker,
                                    m_uniformHandle, 300, 300);

  // ui
  SirMetal::graphics::imguiNewFrame(m_engine, passDescriptor);
  renderDebugWindow();
  SirMetal::graphics::imguiEndFrame(commandBuffer, commandEncoder);

   */
  [commandEncoder endEncoding];

  [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> commandBuffer) {
    // GPU work is complete
    // Signal the semaphore to start the CPU work
    dispatch_semaphore_signal(frameBoundarySemaphore);
  }];
  [commandBuffer presentDrawable:surface];
  [commandBuffer commit];

  //}
}

bool GraphicsLayer::onEvent(SirMetal::Event &) {
  // TODO start processing events the game cares about, like
  // etc...
  return false;
}

void GraphicsLayer::clear() {
  m_engine->m_renderingContext->flush();
  SirMetal::graphics::shutdownImgui();
}
void GraphicsLayer::updateLightData() {
  simd_float3 view{0, 1, 1};
  simd_float3 up{0, 1, 0};
  simd_float3 cross = simd_normalize(simd_cross(up, view));
  up = simd_normalize(simd_cross(view, cross));
  simd_float4 pos4{0, 15, 15, 1};

  simd_float4 view4{view.x, view.y, view.z, 0};
  simd_float4 up4{up.x, up.y, up.z, 0};
  simd_float4 cross4{cross.x, cross.y, cross.z, 0};
  light.V = {cross4, up4, view4, pos4};
  light.P = matrix_float4x4_perspective(1, M_PI / 2, 0.01f, 40.0f);
  // light.VInverse = simd_inverse(light.V);
  light.VP = simd_mul(light.P, simd_inverse(light.V));
  light.lightDir = view4;

  m_engine->m_constantBufferManager->update(m_engine, m_lightHandle, &light);
}
void GraphicsLayer::renderDebugWindow() {

  static bool p_open = false;
  if (m_engine->m_inputManager->isKeyReleased(SDL_SCANCODE_GRAVE)) {
    p_open = !p_open;
  }
  if (!p_open) {
    return;
  }
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Once);
  if (ImGui::Begin("Debug", &p_open, 0)) {
  }
  ImGui::End();
}
} // namespace Sandbox
