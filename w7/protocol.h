#pragma once
#include <enet/enet.h>
#include <cstdint>
#include "entity.h"

enum MessageType : uint8_t
{
  E_CLIENT_TO_SERVER_JOIN = 0,
  E_SERVER_TO_CLIENT_NEW_ENTITY,
  E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY,
  E_CLIENT_TO_SERVER_INPUT,
  E_SERVER_TO_CLIENT_SNAPSHOT,
  E_SERVER_TO_CLIENT_TIME_MSEC,
  E_CLIENT_TO_SERVER_ACK_SNAPSHOT,
  E_CLIENT_TO_SERVER_SHORT_SNAPSHOT
};

struct ShortEntityState
{
  uint16_t eid;
  float x = 0;
  float y = 0;
  float ori = 0;
};

struct EntityState : public ShortEntityState
{
  float vx = 0;
  float vy = 0;
  float omega = 0;
};

void send_join(ENetPeer *peer);
void send_new_entity(ENetPeer *peer, const Entity &ent);
void send_set_controlled_entity(ENetPeer *peer, uint16_t eid);
void send_entity_input(ENetPeer *peer, uint16_t eid, float thr, float steer);
void send_snapshot(ENetPeer *peer, const EntityState& ent, size_t lod);
void send_short_snapshot(ENetPeer *peer, const ShortEntityState& ent);
void send_ack_snapshot(ENetPeer *peer, uint16_t eid);
void send_time_msec(ENetPeer *peer, uint32_t timeMsec);

MessageType get_packet_type(ENetPacket *packet);

void deserialize_new_entity(ENetPacket *packet, Entity &ent);
void deserialize_set_controlled_entity(ENetPacket *packet, uint16_t &eid);
void deserialize_entity_input(ENetPacket *packet, uint16_t &eid, float &thr, float &steer);
void deserialize_snapshot(ENetPacket *packet, EntityState& ent);
void deserialize_short_snapshot(ENetPacket *packet, ShortEntityState& ent);
void deserialize_ack_snapshot(ENetPacket *packet, uint16_t &eid);
void deserialize_time_msec(ENetPacket *packet, uint32_t &timeMsec);

