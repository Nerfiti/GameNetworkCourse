#include <enet/enet.h>
#include <iostream>
#include "entity.h"
#include "protocol.h"
#include <stdlib.h>
#include <vector>
#include <map>
#include <math.h>

static std::vector<Entity> entities;
static std::map<uint16_t, ENetPeer*> controlledMap;

static uint16_t create_random_entity()
{
  uint16_t newEid = entities.size();
  uint32_t color = 0x000000ff +
                   0x44000000 * (1 + rand() % 4) +
                   0x00440000 * (1 + rand() % 4) +
                   0x00004400 * (1 + rand() % 4);
  float x = (rand() % 40 - 20) * 5.f;
  float y = (rand() % 40 - 20) * 5.f;
  float score = (rand() % 30 + 10);
  Entity ent = {color, x, y, newEid, false, 0.f, 0.f, score};
  entities.push_back(ent);
  return newEid;
}

void on_join(ENetPacket *packet, ENetPeer *peer, ENetHost *host)
{
  for (const Entity &ent : entities)
    send_new_entity(peer, ent);

  uint16_t newEid = create_random_entity();
  entities[newEid].active_player = true;
  const Entity& ent = entities[newEid];

  controlledMap[newEid] = peer;

  for (size_t i = 0; i < host->peerCount; ++i)
    send_new_entity(&host->peers[i], ent);

  send_set_controlled_entity(peer, newEid);
}

void on_state(ENetPacket *packet)
{
  uint16_t eid = invalid_entity;
  float x = 0.f; float y = 0.f;
  deserialize_entity_state(packet, eid, x, y);
  for (Entity &e : entities)
  {
    if (e.eid == eid)
    {
      e.x = x;
      e.y = y;
    }
  }
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

  constexpr int numAi = 10;

  for (int i = 0; i < numAi; ++i)
  {
    uint16_t eid = create_random_entity();
    entities[eid].serverControlled = true;
    controlledMap[eid] = nullptr;
  }

  uint32_t lastTime = enet_time_get();
  while (true)
  {
    uint32_t curTime = enet_time_get();
    float dt = (curTime - lastTime) * 0.001f;
    lastTime = curTime;
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
          case E_CLIENT_TO_SERVER_STATE:
            on_state(event.packet);
            break;
        };
        enet_packet_destroy(event.packet);
        break;
      default:
        break;
      };
    }
    for (Entity &e : entities)
    {
      if (e.score > 20)
      {
        e.score -= e.score * dt / 20.f;
        if (controlledMap.contains(e.eid) && controlledMap[e.eid])
          send_snapshot(controlledMap[e.eid], e.eid, e.x, e.y, e.score);
      }
      if (e.serverControlled)
      {
        const float diffX = e.targetX - e.x;
        const float diffY = e.targetY - e.y;
        const float dirX = diffX > 0.f ? 1.f : -1.f;
        const float dirY = diffY > 0.f ? 1.f : -1.f;
        constexpr float spd = 50.f;
        e.x += dirX * spd * dt;
        e.y += dirY * spd * dt;
        if (fabsf(diffX) < 10.f && fabsf(diffY) < 10.f)
        {
          e.targetX = (rand() % 40 - 20) * 15.f;
          e.targetY = (rand() % 40 - 20) * 15.f;
        }
      }

      for (Entity &other : entities)
      {
        if (other.eid == e.eid)
          continue;

        float offset = (e.score - other.score) / 2;
        float dist_to_collision = (e.score + other.score) / 2;
        float dx = e.x - other.x + offset;
        float dy = e.y - other.y + offset;
        if (fabsf(dx) < dist_to_collision && fabsf(dy) < dist_to_collision)
        {
          Entity &winner = (e.score > other.score) ? e : other;
          Entity &looser = (e.score > other.score) ? other : e;

          if (winner.score < 100)
          {
            winner.score += looser.score / 3;

            if (looser.score > 10)
              looser.score /= 2.f;
            if (looser.score > 20)
              looser.score = 20;
          }
          looser.x = (rand() % 40 - 20) * 5.f;
          looser.y = (rand() % 40 - 20) * 5.f;

          if (controlledMap.contains(winner.eid) && controlledMap[winner.eid])
            send_snapshot(controlledMap[winner.eid], winner.eid, winner.x, winner.y, winner.score);

          if (controlledMap.contains(looser.eid) && controlledMap[looser.eid])
            send_snapshot(controlledMap[looser.eid], looser.eid, looser.x, looser.y, looser.score);
        }
      }
    }
    for (const Entity &e : entities)
    {
      for (size_t i = 0; i < server->peerCount; ++i)
      {
        ENetPeer *peer = &server->peers[i];
        if (!peer || controlledMap[e.eid] == peer) continue;
        send_snapshot(peer, e.eid, e.x, e.y, e.score);
      }
    }
    // usleep(1000);
  }

  enet_host_destroy(server);

  atexit(enet_deinitialize);
  return 0;
}


