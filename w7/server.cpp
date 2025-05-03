#include <enet/enet.h>
#include <iostream>
#include "entity.h"
#include "protocol.h"
#include "mathUtils.h"
#include <stdlib.h>
#include <vector>
#include <map>
#include <unordered_map>

struct ACK_id
{
  ENetPeer *peer;
  uint16_t eid;

  bool operator==(const ACK_id &other) const
  {
    return peer == other.peer && eid == other.eid; 
  }
};

template <>
struct std::hash<ACK_id>
{
  std::size_t operator()(const ACK_id& key) const
  {
    return std::hash<uint16_t>()(key.eid) + std::hash<ENetPeer*>()(key.peer);
  }
};


static std::vector<Entity> entities;
static std::map<uint16_t, ENetPeer*> controlledMap;
static std::unordered_map<ACK_id, bool> ACKed_snapshots;

static void init_acked_snapshots_with_new_entity(uint16_t newEid, ENetHost *host, bool withNewPeer, ENetPeer *newPeer = nullptr)
{
  if (withNewPeer)
  {
    for (Entity &e : entities)
    {
      ACK_id id = {.peer = newPeer, .eid = e.eid};
      ACKed_snapshots[id] = false;
    }
  }

  for (size_t i = 0; i < host->peerCount; ++i)
  {
    ACK_id id = {.peer = &host->peers[i], .eid = newEid};
    ACKed_snapshots[id] = false;
  }
}

void on_join(ENetPacket *packet, ENetPeer *peer, ENetHost *host)
{
  for (const Entity &ent : entities)
    send_new_entity(peer, ent);

  uint16_t maxEid = entities.empty() ? invalid_entity : entities[0].eid;
  for (const Entity &e : entities)
    maxEid = std::max(maxEid, e.eid);
  uint16_t newEid = maxEid + 1;

  init_acked_snapshots_with_new_entity(newEid, host, true, peer);

  uint32_t color = 0x000000ff +
                   0x44000000 * (rand() % 4 + 1) +
                   0x00440000 * (rand() % 4 + 1) +
                   0x00004400 * (rand() % 4 + 1);
  float x = (rand() % 4) * 5.f;
  float y = (rand() % 4) * 5.f;
  Entity ent = {color, false, x, y, 0.f, (rand() / RAND_MAX) * 3.141592654f, 0.f, 0.f, 0.f, 0.f, newEid};
  entities.push_back(ent);

  controlledMap[newEid] = peer;

  for (size_t i = 0; i < host->peerCount; ++i)
    send_new_entity(&host->peers[i], ent);

  send_set_controlled_entity(peer, newEid);
}

void create_server_entity(ENetHost *host)
{
  uint16_t maxEid = entities.empty() ? invalid_entity : entities[0].eid;
  for (const Entity &e : entities)
    maxEid = std::max(maxEid, e.eid);
  uint16_t newEid = maxEid + 1;

  init_acked_snapshots_with_new_entity(newEid, host, false);

  uint32_t color = 0xff000000 +
                   0x00440000 * (rand() % 5) +
                   0x00004400 * (rand() % 5) +
                   0x00000044 * (rand() % 5);
  float x = rand() % int(worldSize * 2) - worldSize;
  float y = rand() % int(worldSize * 2) - worldSize;
  Entity ent = {color, true, x, y, 0.f, (rand() / RAND_MAX) * 3.141592654f, 0.f, 0.f, 0.f, 0.f, newEid};
  entities.push_back(ent);

  for (size_t i = 0; i < host->peerCount; ++i)
    send_new_entity(&host->peers[i], ent);
}

void on_input(ENetPacket *packet)
{
  uint16_t eid = invalid_entity;
  float thr = 0.f; float steer = 0.f;
  deserialize_entity_input(packet, eid, thr, steer);
  for (Entity &e : entities)
  {
    if (e.eid == eid)
    {
      e.thr = thr;
      e.steer = steer;
    }
  }
}

void on_ack_snapshot(ENetPacket *packet, ENetPeer *peer)
{
  uint16_t eid = 0;
  deserialize_ack_snapshot(packet, eid);
  ACK_id id = {.peer = peer, .eid = eid};
  ACKed_snapshots.at(id) = true;
}

void on_short_snapshot(ENetPacket *packet, ENetPeer *peer)
{
  ShortEntityState state = {};
  deserialize_short_snapshot(packet, state);

  Entity player_entity = {};
  for (Entity &ent : entities)
  {
    if (controlledMap[ent.eid] == peer)
      player_entity = ent;
  }

  for (Entity &ent : entities)
  {
    if (ent.eid != state.eid)
      continue;

    float dist_to_player = std::max(abs(ent.x - player_entity.x), abs(ent.y - player_entity.y));
    size_t lod = (dist_to_player < 20) ? 0 : (dist_to_player < 40) ? 1 : (dist_to_player < 60) ? 2 : 3;

    float max_x_error = (lod == 0) ? 0.1 : (lod == 1) ? 0.5 : (lod == 2) ? 1 : 10;
    float max_y_error = (lod == 0) ? 0.1 : (lod == 1) ? 0.5 : (lod == 2) ? 1 : 10;
    float max_ori_error = (lod == 0) ? PI / 32 : (lod == 1) ? PI / 16 : (lod == 2) ? PI / 8 : PI / 4;

    bool need_snapshot = abs(state.x - ent.x) > max_x_error || 
                         abs(state.y - ent.y) > max_y_error ||
                         abs(state.ori - ent.ori) > max_ori_error;

    if (need_snapshot)
    {
      EntityState e = {};
      e.eid = ent.eid;
      e.x = ent.x;
      e.y = ent.y;
      e.ori = ent.ori;
      e.vx = ent.vx;
      e.vy = ent.vy;
      e.omega = ent.omega;
      send_snapshot(peer, e, lod);

      ACK_id id = {.peer = peer, .eid = state.eid};
      ACKed_snapshots.at(id) = false;
    }

    break;
  }
}

static void update_net(ENetHost* server)
{
  ENetEvent event;
  while (enet_host_service(server, &event, 0) > 0)
  {
    switch (event.type)
    {
    case ENET_EVENT_TYPE_CONNECT:
      printf("Connection with %x:%u established\n", event.peer->address.host, event.peer->address.port);
      break;
    case ENET_EVENT_TYPE_RECEIVE:
      switch (get_packet_type(event.packet))
      {
        case E_CLIENT_TO_SERVER_JOIN:
          on_join(event.packet, event.peer, server);
          break;
        case E_CLIENT_TO_SERVER_INPUT:
          on_input(event.packet);
          break;
        case E_CLIENT_TO_SERVER_ACK_SNAPSHOT:
          on_ack_snapshot(event.packet, event.peer);
          break;
        case E_CLIENT_TO_SERVER_SHORT_SNAPSHOT:
          on_short_snapshot(event.packet, event.peer);
          break;
      };
      enet_packet_destroy(event.packet);
      break;
    default:
      break;
    };
  }
}

static void update_ai(Entity& e, float dt)
{
  if (rand() % 100 == 0)
    e.thr = e.thr > 0.f ? 0.f : 1.f;
  if (rand() % 10 == 0)
    e.steer = e.steer != 0.f ? 0.f : ((rand() % 2) * 2.f - 1.f);
}

static void simulate_world(ENetHost* server, float dt)
{
  for (Entity &e : entities)
  {
    if (e.serverControlled)
      update_ai(e, dt);

    simulate_entity(e, dt);
  }
}

static void update_time(ENetHost* server, uint32_t curTime)
{
  // We can send it less often too
  for (size_t i = 0; i < server->peerCount; ++i)
    send_time_msec(&server->peers[i], curTime);
}

int main(int argc, const char **argv)
{
  if (enet_initialize() != 0)
  {
    printf("Cannot init ENet");
    return 1;
  }
  ENetAddress address;

  address.host = ENET_HOST_ANY;
  address.port = 10131;

  ENetHost *server = enet_host_create(&address, 32, 2, 0, 0);

  if (!server)
  {
    printf("Cannot create ENet server\n");
    return 1;
  }

  constexpr size_t numShips = 100;
  for (size_t i = 0; i < numShips; ++i)
    create_server_entity(server);

  uint32_t lastTime = enet_time_get();
  while (true)
  {
    uint32_t curTime = enet_time_get();
    float dt = (curTime - lastTime) * 0.001f;
    lastTime = curTime;

    update_net(server);
    simulate_world(server, dt);
    update_time(server, curTime);
    usleep(10000);
  }

  enet_host_destroy(server);

  atexit(enet_deinitialize);
  return 0;
}


