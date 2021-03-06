#include "graphicsLayer.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SirMetal/core/mathUtils.h>
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


static const NSUInteger kMaxInflightBuffers = 3;
static constexpr int shadowAlgorithmsCount = 2;
static const char *shadowAlgorithms[] = {"PCF", "PCSS"};
static constexpr float pcssPcfSizeDefault = 6.5f;
static constexpr float pcfSizeDefault = 0.005f;

namespace Sandbox {
void GraphicsLayer::onAttach(SirMetal::EngineContext *context) {

  m_engine = context;
  // initializing the camera to the identity
  m_camera.viewMatrix = matrix_float4x4_translation(vector_float3{0, 0, 0});
  m_camera.fov = M_PI / 4;
  m_camera.nearPlane = 0.01;
  m_camera.farPlane = 60;
  m_cameraController.setCamera(&m_camera);
  m_cameraController.setPosition(3, 5, 15);
  m_camConfig = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2, 0.008};

  m_camUniformHandle = m_engine->m_constantBufferManager->allocate(
      m_engine, sizeof(SirMetal::Camera),
      SirMetal::CONSTANT_BUFFER_FLAGS_BITS::CONSTANT_BUFFER_FLAG_BUFFERED);
  m_lightHandle = m_engine->m_constantBufferManager->allocate(
      m_engine, sizeof(DirLight),
      SirMetal::CONSTANT_BUFFER_FLAGS_BITS::CONSTANT_BUFFER_FLAG_NONE);

  light.lightSize = 0.025f;
  light.near = 0.2f;
  light.pcfsize = 0.005;
  light.pcfsamples = 64;
  light.blockerCount = 64;
  light.algType = 0;
  updateLightData();

  const std::string base = m_engine->m_config.m_dataSourcePath;
  const std::string baseSample = base + "/02_pcf_pcss";

  const char *names[5] = {"/plane.obj", "/cube.obj", "/lucy.obj", "/cone.obj",
                          "/cilinder.obj"};
  for (int i = 0; i < 5; ++i) {
    m_meshes[i] = m_engine->m_meshManager->loadMesh(baseSample + names[i]);
  }

  id<MTLDevice> device = m_engine->m_renderingContext->getDevice();

  SirMetal::AllocTextureRequest requestDepth{
      m_engine->m_config.m_windowConfig.m_width,
      m_engine->m_config.m_windowConfig.m_height,
      1,
      MTLTextureType2D,
      MTLPixelFormatDepth32Float_Stencil8,
      MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
      MTLStorageModePrivate,
      1,
      "depthTexture"};
  m_depthHandle = m_engine->m_textureManager->allocate(device, requestDepth);
  requestDepth.width = 2048;
  requestDepth.height = 2048;
  requestDepth.format = MTLPixelFormatDepth32Float;
  requestDepth.name = "shadowMap";
  m_shadowHandle = m_engine->m_textureManager->allocate(device, requestDepth);

  m_shaderHandle =
      m_engine->m_shaderManager->loadShader((baseSample + "/Shaders.metal").c_str());

  m_shadowShaderHandle =
      m_engine->m_shaderManager->loadShader((baseSample + "/shadows.metal").c_str());
  SirMetal::graphics::initImgui(m_engine);

  frameBoundarySemaphore = dispatch_semaphore_create(kMaxInflightBuffers);
}

void GraphicsLayer::onDetach() {}

void GraphicsLayer::updateUniformsForView(float screenWidth,
                                          float screenHeight) {

  SirMetal::Input *input = m_engine->m_inputManager;

  if (!ImGui::GetIO().WantCaptureMouse) {
    m_cameraController.update(m_camConfig, input);
  }

  m_cameraController.updateProjection(screenWidth, screenHeight);
  m_engine->m_constantBufferManager->update(m_engine, m_camUniformHandle,
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

  id<MTLDevice> device = m_engine->m_renderingContext->getDevice();
  SirMetal::graphics::DrawTracker shadowTracker{};
  shadowTracker.renderTargets[0] = nil;
  shadowTracker.depthTarget =
      m_engine->m_textureManager->getNativeFromHandle(m_shadowHandle);
  SirMetal::PSOCache cache = SirMetal::getPSO(
      m_engine, shadowTracker, SirMetal::Material{"shadows", false});

  id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
  [commandBuffer setLabel:@"testName"];
  // shadows
  MTLRenderPassDescriptor *shadowPassDescriptor =
      [MTLRenderPassDescriptor renderPassDescriptor];
  shadowPassDescriptor.colorAttachments[0].texture = nil;
  shadowPassDescriptor.colorAttachments[0].clearColor = {};
  shadowPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionDontCare;
  shadowPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionDontCare;

  MTLRenderPassDepthAttachmentDescriptor *depthAttachment =
      shadowPassDescriptor.depthAttachment;
  depthAttachment.texture =
      m_engine->m_textureManager->getNativeFromHandle(m_shadowHandle);
  depthAttachment.clearDepth = 1.0;
  depthAttachment.storeAction = MTLStoreActionDontCare;
  depthAttachment.loadAction = MTLLoadActionClear;

  // shadow pass
  id<MTLRenderCommandEncoder> shadowEncoder =
      [commandBuffer renderCommandEncoderWithDescriptor:shadowPassDescriptor];

  [shadowEncoder setRenderPipelineState:cache.color];
  [shadowEncoder setDepthStencilState:cache.depth];
  [shadowEncoder setFrontFacingWinding:MTLWindingCounterClockwise];
  [shadowEncoder setCullMode:MTLCullModeBack];

  SirMetal::BindInfo info =
      m_engine->m_constantBufferManager->getBindInfo(m_engine, m_lightHandle);
  [shadowEncoder setVertexBuffer:info.buffer offset:info.offset atIndex:4];

  for (auto &mesh : m_meshes) {
    const SirMetal::MeshData *meshData =
        m_engine->m_meshManager->getMeshData(mesh);

    [shadowEncoder setVertexBuffer:meshData->vertexBuffer
                            offset:meshData->ranges[0].m_offset
                           atIndex:0];
    [shadowEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                              indexCount:meshData->primitivesCount
                               indexType:MTLIndexTypeUInt32
                             indexBuffer:meshData->indexBuffer
                       indexBufferOffset:0];
  }
  [shadowEncoder endEncoding];

  // render
  SirMetal::graphics::DrawTracker tracker{};
  tracker.renderTargets[0] = texture;
  tracker.depthTarget =
      m_engine->m_textureManager->getNativeFromHandle(m_depthHandle);
  cache =
      SirMetal::getPSO(m_engine, tracker, SirMetal::Material{"Shaders", false});

  MTLRenderPassDescriptor *passDescriptor =
      [MTLRenderPassDescriptor renderPassDescriptor];

  passDescriptor.colorAttachments[0].texture = texture;
  passDescriptor.colorAttachments[0].clearColor = {0.2, 0.2, 0.2, 1.0};
  passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
  passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;

  depthAttachment = passDescriptor.depthAttachment;
  depthAttachment.texture =
      m_engine->m_textureManager->getNativeFromHandle(m_depthHandle);
  depthAttachment.clearDepth = 1.0;
  depthAttachment.storeAction = MTLStoreActionDontCare;
  depthAttachment.loadAction = MTLLoadActionClear;
  id<MTLRenderCommandEncoder> commandEncoder =
      [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];

  [commandEncoder setRenderPipelineState:cache.color];
  [commandEncoder setDepthStencilState:cache.depth];
  [commandEncoder setFrontFacingWinding:MTLWindingCounterClockwise];
  [commandEncoder setCullMode:MTLCullModeBack];

  info =
      m_engine->m_constantBufferManager->getBindInfo(m_engine,
                                                        m_camUniformHandle);
  [commandEncoder setVertexBuffer:info.buffer offset:info.offset atIndex:4];
  info =
      m_engine->m_constantBufferManager->getBindInfo(m_engine, m_lightHandle);
  [commandEncoder setFragmentBuffer:info.buffer offset:info.offset atIndex:5];
  id<MTLTexture> shadow =
      m_engine->m_textureManager->getNativeFromHandle(m_shadowHandle);
  [commandEncoder setFragmentTexture:shadow atIndex:0];

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
                                    m_camUniformHandle, 300, 300);

  // ui
  SirMetal::graphics::imguiNewFrame(m_engine, passDescriptor);
  renderDebugWindow();
  SirMetal::graphics::imguiEndFrame(commandBuffer, commandEncoder);

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
    if(m_shadowAlgorithmsLast != light.algType)
    {
      light.pcfsize = pcfSizeDefault;
    }
    m_timingsWidget.render(m_engine);
    // Main body of the Demo window starts here.
    // Early out if the window is collapsed, as an optimization.
    ImGui::Combo("Algorithm", &light.algType, shadowAlgorithms,
                 shadowAlgorithmsCount);
    if (light.algType == 0) {
      if(m_shadowAlgorithmsLast != light.algType)
      {
        light.pcfsize = pcfSizeDefault;
      }
      ImGui::SliderFloat("pcfSize", &light.pcfsize, 0.0f, 0.1f);
      ImGui::SliderInt("pcfSamples", &light.pcfsamples, 1, 64);
    } else {
      if(m_shadowAlgorithmsLast != light.algType)
      {
        light.pcfsize = pcssPcfSizeDefault;
      }
      ImGui::SliderFloat("lightSize", &light.lightSize, 0.0f, 0.2f);
      ImGui::SliderFloat("near", &light.near, 0.0f, 1.0f);
      ImGui::SliderFloat("penumbraMultiplier", &light.pcfsize, 0.0f, 10.2f);
      ImGui::SliderInt("pcfsamples", &light.pcfsamples, 1, 64);
      ImGui::SliderInt("blockerCount", &light.blockerCount, 1, 64);
      ImGui::Checkbox("showBlocker", (bool *)&light.showBlocker);
    }
  }
  m_shadowAlgorithmsLast = light.algType;
  ImGui::End();
}
} // namespace Sandbox
