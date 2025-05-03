#pragma once
#include <math.h>

inline float move_to(float from, float to, float dt, float vel)
{
  float d = vel * dt;
  if (fabsf(from - to) < d)
    return to;

  if (to < from)
    return from - d;
  else
    return from + d;
}

inline float clamp(float in, float min, float max)
{
  return in < min ? min : in > max ? max : in;
}

inline float sign(float in)
{
  return in > 0.f ? 1.f : in < 0.f ? -1.f : 0.f;
}

inline float tile_val(float val, float border)
{
  if (val < -border)
    return val + 2.f * border;
  else if (val > border)
    return val - 2.f * border;
  return val;
}

#undef PI
constexpr float PI = 3.141592654f;

