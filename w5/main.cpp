// initial skeleton is a clone from https://github.com/jpcy/bgfx-minimal-example
//
#include <iostream>

#include <functional>
#include "raylib.h"
#include <enet/enet.h>
#include <math.h>

#include <vector>
#include "entity.h"
#include "protocol.h"

struct Snapshot
{
    float x, y, ori;
    uint32_t timestamp;
};

struct PredictedEntity
{
    Entity state;
    std::vector<std::pair<uint32_t, Entity>> inputHistory;
};

static std::vector<Entity> entities;
static std::unordered_map<uint16_t, size_t> indexMap;
static std::unordered_map<uint16_t, std::vector<Snapshot>> snapshotHistory;
static std::unordered_map<uint16_t, PredictedEntity> predictedEntities;
static uint16_t my_entity = invalid_entity;

constexpr uint32_t interpolationDelay = 200;
constexpr uint32_t maxHistorySize = 10;
constexpr float fixedDt = 1.f / 60.f;

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

void on_snapshot(ENetPacket *packet)
{
  uint16_t eid = invalid_entity;
  float x = 0.f, y = 0.f, ori = 0.f;
  uint32_t frame = 0;
  deserialize_snapshot(packet, eid, x, y, ori, frame);

  if (eid == my_entity)
  {
    auto& pred = predictedEntities[eid];
    auto it = std::find_if(pred.inputHistory.begin(), pred.inputHistory.end(),
      [frame](const auto& entry) { return entry.first >= frame; });

    if (it != pred.inputHistory.end())
    {
      Entity corrected;
      corrected.x = x;
      corrected.y = y;
      corrected.ori = ori;

      for (; it != pred.inputHistory.end(); ++it)
      {
        corrected.thr = it->second.thr;
        corrected.steer = it->second.steer;
        simulate_entity(corrected, fixedDt);
      }

      pred.state = corrected;
    }

    pred.inputHistory.erase(pred.inputHistory.begin(), it);
  }
  else
  {
    auto& history = snapshotHistory[eid];
    history.push_back({x, y, ori, frame});

    while (history.size() > maxHistorySize)
      history.erase(history.begin());
  }
}

void on_full_state(ENetPacket *packet)
{
  uint16_t eid;
  float x, y, vx, vy, ori, omega;
  uint32_t frame;
  deserialize_full_state(packet, eid, x, y, vx, vy, ori, omega, frame);

  if (eid != my_entity) return;

  auto& pred = predictedEntities[eid];

  auto it = std::find_if(pred.inputHistory.begin(), pred.inputHistory.end(),
      [frame](const auto& entry) { return entry.first == frame; });

  if (it == pred.inputHistory.end()) return;

  size_t framesToCorrect = std::distance(it, pred.inputHistory.end());
  if (framesToCorrect == 0) return;

  float dx = (x - it->second.x) / framesToCorrect;
  float dy = (y - it->second.y) / framesToCorrect;
  float dvx = (vx - it->second.vx) / framesToCorrect;
  float dvy = (vy - it->second.vy) / framesToCorrect;
  float dori = (ori - it->second.ori) / framesToCorrect;
  float domega = (omega - it->second.omega) / framesToCorrect;

  size_t i = 1;
  for (; it != pred.inputHistory.end(); ++it, ++i) {
      it->second.x += dx * i;
      it->second.y += dy * i;
      it->second.vx += dvx * i;
      it->second.vy += dvy * i;
      it->second.ori += dori * i;
      it->second.omega += domega * i;
  }

  pred.state.x = x;
  pred.state.y = y;
  pred.state.vx = vx;
  pred.state.vy = vy;
  pred.state.ori = ori;
  pred.state.omega = omega;
}

static void on_time(ENetPacket *packet, ENetPeer* peer)
{
  uint32_t timeMsec;
  deserialize_time_msec(packet, timeMsec);
  enet_time_set(timeMsec + peer->lastRoundTripTime / 2);
}

static void draw_entity(const Entity& e)
{
  const float shipLen = 3.f;
  const float shipWidth = 2.f;
  const Vector2 fwd = Vector2{cosf(e.ori), sinf(e.ori)};
  const Vector2 left = Vector2{-fwd.y, fwd.x};
  DrawTriangle(Vector2{e.x + fwd.x * shipLen * 0.5f, e.y + fwd.y * shipLen * 0.5f},
               Vector2{e.x - fwd.x * shipLen * 0.5f - left.x * shipWidth * 0.5f, e.y - fwd.y * shipLen * 0.5f - left.y * shipWidth * 0.5f},
               Vector2{e.x - fwd.x * shipLen * 0.5f + left.x * shipWidth * 0.5f, e.y - fwd.y * shipLen * 0.5f + left.y * shipWidth * 0.5f},
               GetColor(e.color));
}

void update_net(ENetHost* client, ENetPeer* serverPeer)
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
        on_snapshot(event.packet);
        break;
      case E_SERVER_TO_CLIENT_TIME_MSEC:
        on_time(event.packet, event.peer);
        break;
      case E_SERVER_TO_CLIENT_FULL_STATE: // Added new case
        on_full_state(event.packet);
        break;
      };
      enet_packet_destroy(event.packet);
      break;
    default:
      break;
    };
  }
}

void simulate_world(ENetPeer* serverPeer)
{
  if (my_entity != invalid_entity)
  {
    bool left = IsKeyDown(KEY_LEFT);
    bool right = IsKeyDown(KEY_RIGHT);
    bool up = IsKeyDown(KEY_UP);
    bool down = IsKeyDown(KEY_DOWN);

    get_entity(my_entity, [&](Entity& e)
    {
      float thr = (up ? 1.f : 0.f) + (down ? -1.f : 0.f);
      float steer = (left ? -1.f : 0.f) + (right ? 1.f : 0.f);

      uint32_t currentFrame = enet_time_get() / (fixedDt * 1000);
      predictedEntities[my_entity].inputHistory.push_back({currentFrame, e});

      e.thr = thr;
      e.steer = steer;
      simulate_entity(e, fixedDt);

      send_entity_input(serverPeer, my_entity, thr, steer);
    });
  }
}

static void draw_world(const Camera2D& camera)
{
  BeginDrawing();
  ClearBackground(GRAY);
  BeginMode2D(camera);

  uint32_t currentTime = enet_time_get() - interpolationDelay;

  // Draw interpolated entities
  for (const auto& pair : snapshotHistory)
  {
    uint16_t eid = pair.first;
    const auto& history = pair.second;

    if (history.empty()) continue;

    const Snapshot* prev = nullptr;
    const Snapshot* next = nullptr;

    for (size_t i = 0; i < history.size(); ++i) {
      if (history[i].timestamp <= currentTime) {
        prev = &history[i];
        if (i + 1 < history.size() && history[i+1].timestamp > currentTime)
        {
          next = &history[i+1];
          break;
        }
      }
    }

    if (!prev) continue;

    Entity e;
    e.color = entities[eid].color;
    if (next)
    {
      float t = float(currentTime - prev->timestamp) / float(next->timestamp - prev->timestamp);
      e.x = prev->x + (next->x - prev->x) * t;
      e.y = prev->y + (next->y - prev->y) * t;
      e.ori = prev->ori + (next->ori - prev->ori) * t;
    }
    else
    {
      e.x = prev->x;
      e.y = prev->y;
      e.ori = prev->ori;
    }

    draw_entity(e);
  }

  // Draw predicted player entity
  if (my_entity != invalid_entity)
  {
    auto it = predictedEntities.find(my_entity);
    if (it != predictedEntities.end())
    {
      draw_entity(it->second.state);
    }
  }

  EndMode2D();
  EndDrawing();
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

  InitWindow(width, height, "w5 networked MIPT");

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

  while (!WindowShouldClose())
  {
    float dt = GetFrameTime(); // for future use and making it look smooth

    update_net(client, serverPeer);
    simulate_world(serverPeer);
    draw_world(camera);
    printf("%d\n", enet_time_get());
  }

  CloseWindow();
  return 0;
}
