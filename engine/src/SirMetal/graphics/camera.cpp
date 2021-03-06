
#import <simd/simd.h>

#include "SDL.h"
#import "SirMetal/core/input.h"
#include "SirMetal/core/mathUtils.h"
#import "SirMetal/engine.h"
#include "SirMetal/graphics/camera.h"

namespace SirMetal {

bool FPSCameraController::update(const CameraManipulationConfig &camConfig,
                                 Input *input) {

  // resetting position
  simd_float4 pos = m_camera->viewMatrix.columns[3];
  m_camera->viewMatrix.columns[3] = simd_float4{0, 0, 0, 1};

  // applying rotation if mouse is pressed
  bool xMoved = input->mouse.position.x != input->mouse.positionPrev.x;
  bool yMoved = input->mouse.position.y != input->mouse.positionPrev.y;
  bool moved = xMoved | yMoved;
  bool camMoved = false;
  if (input->mouse.buttons[SDL_BUTTON_LEFT] & moved) {

    camMoved = true;
    auto up = simd_float4{0, 1, 0, 0};
    auto xRel= input->mouse.position.xRel;
    float maxV = 5;
    xRel = xRel> maxV ? maxV  : xRel;
    xRel = xRel < -maxV ? -maxV :xRel;
    auto yRel= input->mouse.position.yRel;
    yRel = yRel> maxV ? maxV  : yRel;
    yRel = yRel < -maxV ? -maxV :yRel;
    const float rotationYFactor = xRel *
                                  camConfig.leftRightLookDirection *
                                  camConfig.lookSpeed;
    const matrix_float4x4 camRotY = matrix_float4x4_rotation(
        vector_float3{up.x, up.y, up.z}, rotationYFactor);
    m_camera->viewMatrix = matrix_multiply(camRotY, m_camera->viewMatrix);

    simd_float4 side = m_camera->viewMatrix.columns[0];
    const float rotationXFactor = yRel*
                                  camConfig.upDownLookDirection *
                                  camConfig.lookSpeed;
    const matrix_float4x4 camRotX = matrix_float4x4_rotation(
        vector_float3{side.x, side.y, side.z}, rotationXFactor);
    m_camera->viewMatrix = matrix_multiply(camRotX, m_camera->viewMatrix);
  }

  simd_float4 forward = m_camera->viewMatrix.columns[2];
  simd_float4 side = m_camera->viewMatrix.columns[0];

  // movement factors

  // for each button we apply a factor and apply a transformation
  // if the button is not pressed factor will be zero.
  // This avoids us to do lots of if(). trading some ALU
  // work for branches.
  // moving left and right
  float applicationFactor = input->isKeyDown(SDL_SCANCODE_A);
  float leftRightFactor =
      camConfig.leftRightMovementDirection * camConfig.movementSpeed;
  pos += side * (-leftRightFactor) * applicationFactor;
  camMoved |= (applicationFactor > 0.0f);

  applicationFactor = input->isKeyDown(SDL_SCANCODE_D);
  pos += side * (leftRightFactor)*applicationFactor;
  camMoved |= (applicationFactor > 0.0f);

  // forward and back
  float fbFactor =
      camConfig.forwardBackMovementDirection * camConfig.movementSpeed;
  applicationFactor = input->isKeyDown(SDL_SCANCODE_W);
  pos += forward * (-fbFactor) * applicationFactor;
  camMoved |= (applicationFactor > 0.0f);

  applicationFactor = input->isKeyDown(SDL_SCANCODE_S);
  pos += forward * fbFactor * applicationFactor;
  camMoved |= (applicationFactor > 0.0f);

  // up and down
  float udFactor = camConfig.upDownMovementDirection * camConfig.movementSpeed;
  applicationFactor = input->isKeyDown(SDL_SCANCODE_Q);
  pos += vector_float4{0, 1, 0, 0} * (applicationFactor) * (-udFactor);
  camMoved |= (applicationFactor > 0.0f);

  applicationFactor = input->isKeyDown(SDL_SCANCODE_E);
  pos += vector_float4{0, 1, 0, 0} * applicationFactor * (udFactor);
  camMoved |= (applicationFactor > 0.0f);

  m_camera->viewMatrix.columns[3] = pos;
  m_camera->viewInverse = simd_inverse(m_camera->viewMatrix);

  return camMoved;
}

void FPSCameraController::setPosition(float x, float y, float z) {
  // Should this update the inverse matrix? Up for discussion
  // for now the only place where that happens is in update
  m_camera->viewMatrix.columns[3] = simd_float4{x, y, z, 1.0f};
}

void FPSCameraController::updateProjection(float screenWidth,
                                           float screenHeight) {
  m_camera->screenWidth = screenWidth;
  m_camera->screenHeight = screenHeight;

  const float aspect = screenWidth / screenHeight;
  const float fov = m_camera->fov;
  const float near = m_camera->nearPlane;
  const float far = m_camera->farPlane;
  m_camera->projection = matrix_float4x4_perspective(aspect, fov, near, far);
  m_camera->VP = simd_mul(m_camera->projection, m_camera->viewInverse);
  m_camera->viewInverse = simd_inverse(m_camera->viewMatrix);
  m_camera->VPInverse = simd_inverse(m_camera->VP);
}
} // namespace SirMetal
