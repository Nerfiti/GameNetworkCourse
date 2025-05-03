#pragma once
#include <cstdint>
#include "mathUtils.h"

constexpr uint16_t invalid_entity = -1;
constexpr float worldSize = 120.f;
constexpr float max_speed = worldSize / 3;
constexpr float max_omega = 4 * PI;

struct Entity
{
  // immutable state
  uint32_t color = 0xff00ffff;
  bool serverControlled = false;

  // mutable state
  float x = 0.f;
  float y = 0.f;
  float vx = 0.f;
  float vy = 0.f;
  float ori = 0.f;
  float omega = 0.f;

  // user input
  float thr = 0.f;
  float steer = 0.f;

  // misc
  uint16_t eid = invalid_entity;
};

void simulate_entity(Entity &e, float dt);

