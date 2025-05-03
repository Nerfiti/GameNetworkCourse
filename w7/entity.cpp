#include "entity.h"
#include "mathUtils.h"

void simulate_entity(Entity &e, float dt)
{
  bool isBraking = sign(e.thr) < 0.f;
  float accel = isBraking ? 6.f : 1.5f;
  float va = clamp(e.thr, -0.3, 1.f) * accel;
  e.vx += cosf(e.ori) * va * dt;
  e.vy += sinf(e.ori) * va * dt;
  e.omega += e.steer * dt * 0.3f;

  e.vx = clamp(e.vx, -max_speed, max_speed);
  e.vy = clamp(e.vy, -max_speed, max_speed);
  e.omega = clamp(e.omega, -max_omega, max_omega);

  e.ori += e.omega * dt;
  e.x += e.vx * dt;
  e.y += e.vy * dt;

  e.ori = tile_val(e.ori, PI);
  e.x = tile_val(e.x, worldSize);
  e.y = tile_val(e.y, worldSize);
}

