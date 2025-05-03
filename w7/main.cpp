// initial skeleton is a clone from https://github.com/jpcy/bgfx-minimal-example
//
#include <functional>
#include "raylib.h"
#include <enet/enet.h>
#include <math.h>
#include <iostream>

#include <vector>
#include "entity.h"
#include "protocol.h"


static std::vector<Entity> entities;
static std::unordered_map<uint16_t, size_t> indexMap;
static uint16_t my_entity = invalid_entity;

struct BandwidthAccumulator
{
  std::vector<std::pair<uint32_t, float>> inData;
  std::vector<std::pair<uint32_t, float>> outData;
  float curTime = 0.f;
};

uint32_t get_delta_data(const std::vector<std::pair<uint32_t, float>>& data)
{
  if (data.empty())
    return 0;
  return data.back().first - data.front().first;
}

void on_new_entity_packet(ENetPacket *packet)
{
  Entity newEntity;
  deserialize_new_entity(packet, newEntity);
  auto itf = indexMap.find(newEntity.eid);
  if (itf != indexMap.end())
    return; // don't need to do anything, we already have entity
  indexMap[newEntity.eid] = entities.size();
  entities.push_back(newEntity);
}

void on_set_controlled_entity(ENetPacket *packet)
{
  deserialize_set_controlled_entity(packet, my_entity);
}

template<typename Callable>
static void get_entity(uint16_t eid, Callable c)
{
  auto itf = indexMap.find(eid);
  if (itf != indexMap.end())
    c(entities[itf->second]);
}

void on_snapshot(ENetPacket *packet, ENetPeer *peer)
{
  EntityState state = {};
  deserialize_snapshot(packet, state);
  get_entity(state.eid, [&](Entity& e)
  {
      e.x = state.x;
      e.y = state.y;
      e.ori = state.ori;
      e.vx = state.vx;
      e.vy = state.vy;
      e.omega = state.omega;
  });

  send_ack_snapshot(peer, state.eid);
}

static void on_time(ENetPacket *packet, ENetPeer* peer)
{
  uint32_t timeMsec;
  deserialize_time_msec(packet, timeMsec);
  enet_time_set(timeMsec + peer->lastRoundTripTime / 2);
}

static void draw_ship(float shipLen, float shipWidth, float x, float y, const Vector2& fwd, const Vector2& left, Color col)
{
  DrawTriangle(Vector2{x + fwd.x * shipLen * 0.5f, y + fwd.y * shipLen * 0.5f},
               Vector2{x - fwd.x * shipLen * 0.5f - left.x * shipWidth * 0.5f, y - fwd.y * shipLen * 0.5f - left.y * shipWidth * 0.5f},
               Vector2{x - fwd.x * shipLen * 0.5f + left.x * shipWidth * 0.5f, y - fwd.y * shipLen * 0.5f + left.y * shipWidth * 0.5f},
               col);
}

static void draw_entity(const Entity& e)
{
  const float shipLen = 3.f;
  const float shipWidth = 2.f;
  const Vector2 fwd = Vector2{cosf(e.ori), sinf(e.ori)};
  const Vector2 left = Vector2{-fwd.y, fwd.x};
  Vector3 hsv = ColorToHSV(GetColor(e.color));
  draw_ship(shipLen + 0.4f, shipWidth + 0.4f, e.x, e.y, fwd, left, ColorFromHSV(int(hsv.x + 120.f) % 360, 1.f, 1.f));
  draw_ship(shipLen, shipWidth, e.x, e.y, fwd, left, ColorFromHSV(hsv.x, hsv.y, hsv.z));//GetColor(e.color));
}

static void update_net(ENetHost* client, ENetPeer* serverPeer)
{
  ENetEvent event;
  while (enet_host_service(client, &event, 0) > 0)
  {
    switch (event.type)
    {
    case ENET_EVENT_TYPE_CONNECT:
      printf("Connection with %x:%u established\n", event.peer->address.host, event.peer->address.port);
      send_join(serverPeer);
      break;
    case ENET_EVENT_TYPE_RECEIVE:
      switch (get_packet_type(event.packet))
      {
      case E_SERVER_TO_CLIENT_NEW_ENTITY:
        on_new_entity_packet(event.packet);
        break;
      case E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY:
        on_set_controlled_entity(event.packet);
        break;
      case E_SERVER_TO_CLIENT_SNAPSHOT:
        on_snapshot(event.packet, event.peer);
        break;
      case E_SERVER_TO_CLIENT_TIME_MSEC:
        on_time(event.packet, event.peer);
        break;
      };
      enet_packet_destroy(event.packet);
      break;
    default:
      break;
    };
  }
}

static void simulate_world(ENetPeer* serverPeer, float dt)
{
  if (my_entity != invalid_entity)
  {
    bool left = IsKeyDown(KEY_LEFT);
    bool right = IsKeyDown(KEY_RIGHT);
    bool up = IsKeyDown(KEY_UP);
    bool down = IsKeyDown(KEY_DOWN);

    float thr = (up ? 1.f : 0.f) + (down ? -1.f : 0.f);
    float steer = (left ? -1.f : 0.f) + (right ? 1.f : 0.f);
    send_entity_input(serverPeer, my_entity, thr, steer);

    for (Entity &e : entities)
    {
      if (e.eid == my_entity)
      {
        e.thr = thr;
        e.steer = steer;
        simulate_entity(e, dt);
      }
    }
  }

  for (Entity &e : entities)
  {
    if (e.eid != my_entity)
    {
      e.x += e.vx * dt;
      e.y += e.vy * dt;
      e.ori += e.omega * dt;

      e.x   = tile_val(e.x, worldSize);
      e.y   = tile_val(e.y, worldSize);
      e.ori = tile_val(e.ori, PI);
    }

    ShortEntityState state = {};
    state.eid = e.eid;
    state.x = e.x;
    state.y = e.y;
    state.ori = e.ori;

    send_short_snapshot(serverPeer, state);
  }
}

static void draw_world(const Camera2D& camera, const BandwidthAccumulator& bw)
{
  BeginDrawing();
    ClearBackground(DARKGRAY);
    BeginMode2D(camera);

      DrawRectangleLines(-worldSize, -worldSize, 2.f * worldSize, 2.f * worldSize, WHITE);

      constexpr size_t numGrid = 10;
      for (size_t y = 1; y < numGrid; ++y)
        DrawLine(-worldSize, -worldSize + 2.f * worldSize * (float(y) / numGrid), worldSize, -worldSize + 2.f * worldSize * (float(y) / numGrid), GetColor(0xffffffff));

      for (size_t x = 1; x < numGrid; ++x)
        DrawLine(-worldSize + 2.f * worldSize * (float(x) / numGrid), -worldSize, -worldSize + 2.f * worldSize * (float(x) / numGrid), worldSize, GetColor(0xffffffff));

      for (const Entity &e : entities)
        draw_entity(e);

    EndMode2D();
    DrawText(TextFormat("Bandwidth: in %0.2f kbit/s", get_delta_data(bw.inData) / 1024.f), 8, 8, 12, WHITE);
    DrawText(TextFormat("Bandwidth: out %0.2f kbit/s", get_delta_data(bw.outData) / 1024.f), 8, 20, 12, WHITE);
  EndDrawing();
}

static void update_camera(Camera2D& camera)
{
  if (my_entity != invalid_entity)
  {
    get_entity(my_entity, [&](Entity& e)
    {
      camera.target.x += (e.x - camera.target.x) * 0.1f;
      camera.target.y += (e.y - camera.target.y) * 0.1f;
    });
  }
  camera.zoom *= (1.f - GetMouseWheelMove() * 0.1f);
}

void update_bandwidth(float dt, ENetHost* host, BandwidthAccumulator& accum)
{
  constexpr float windowSize = 1.f; // 1 sec
  accum.curTime += dt;
  accum.inData.emplace_back(host->totalReceivedData, accum.curTime);
  accum.outData.emplace_back(host->totalSentData, accum.curTime);

  auto eraseOld = [&](std::vector<std::pair<uint32_t, float>>& data)
  {
    while (data.front().second < accum.curTime - windowSize)
      data.erase(data.begin());
  };
  eraseOld(accum.inData);
  eraseOld(accum.outData);
}

int main(int argc, const char **argv)
{
  if (enet_initialize() != 0)
  {
    printf("Cannot init ENet");
    return 1;
  }

  ENetHost *client = enet_host_create(nullptr, 1, 2, 0, 0);
  if (!client)
  {
    printf("Cannot create ENet client\n");
    return 1;
  }

  ENetAddress address;
  enet_address_set_host(&address, "localhost");
  address.port = 10131;

  ENetPeer *serverPeer = enet_host_connect(client, &address, 2, 0);
  if (!serverPeer)
  {
    printf("Cannot connect to server");
    return 1;
  }

  int width = 600;
  int height = 600;

  InitWindow(width, height, "w7 networked MIPT");

  const int scrWidth = GetMonitorWidth(0);
  const int scrHeight = GetMonitorHeight(0);
  if (scrWidth < width || scrHeight < height)
  {
    width = std::min(scrWidth, width);
    height = std::min(scrHeight - 150, height);
    SetWindowSize(width, height);
  }

  Camera2D camera = { {0, 0}, {0, 0}, 0.f, 1.f };
  camera.target = Vector2{ 0.f, 0.f };
  camera.offset = Vector2{ width * 0.5f, height * 0.5f };
  camera.rotation = 0.f;
  camera.zoom = 10.f;

  SetTargetFPS(60);               // Set our game to run at 60 frames-per-second

  BandwidthAccumulator bandwidthAccumulator;
  while (!WindowShouldClose())
  {
    float dt = GetFrameTime();

    update_net(client, serverPeer);
    update_bandwidth(dt, client, bandwidthAccumulator);
    simulate_world(serverPeer, dt);
    update_camera(camera);
    draw_world(camera, bandwidthAccumulator);
  }

  CloseWindow();
  return 0;
}
