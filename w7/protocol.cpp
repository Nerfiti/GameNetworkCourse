#include "protocol.h"
#include "quantisation.h"
#include <cstring> // memcpy
#include <iostream>

static constexpr size_t x_bits_lods[]     = {16, 11, 8, 5};
static constexpr size_t y_bits_lods[]     = {16, 11, 8, 5};
static constexpr size_t ori_bits_lods[]   = {8, 8, 6, 4};
static constexpr size_t vx_bits_lods[]    = {8, 8, 6, 4};
static constexpr size_t vy_bits_lods[]    = {8, 8, 6, 4};
static constexpr size_t omega_bits_lods[] = {8, 8, 6, 4};

static constexpr size_t short_snapshot_default_lod = 0;

void send_join(ENetPeer *peer)
{
  ENetPacket *packet = enet_packet_create(nullptr, sizeof(uint8_t), ENET_PACKET_FLAG_RELIABLE);
  *packet->data = E_CLIENT_TO_SERVER_JOIN;

  enet_peer_send(peer, 0, packet);
}

void send_new_entity(ENetPeer *peer, const Entity &ent)
{
  ENetPacket *packet = enet_packet_create(nullptr, sizeof(uint8_t) + sizeof(Entity),
                                                   ENET_PACKET_FLAG_RELIABLE);
  uint8_t *ptr = packet->data;
  *ptr = E_SERVER_TO_CLIENT_NEW_ENTITY; ptr += sizeof(uint8_t);
  memcpy(ptr, &ent, sizeof(Entity)); ptr += sizeof(Entity);

  enet_peer_send(peer, 0, packet);
}

void send_set_controlled_entity(ENetPeer *peer, uint16_t eid)
{
  ENetPacket *packet = enet_packet_create(nullptr, sizeof(uint8_t) + sizeof(uint16_t),
                                                   ENET_PACKET_FLAG_RELIABLE);
  uint8_t *ptr = packet->data;
  *ptr = E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY; ptr += sizeof(uint8_t);
  memcpy(ptr, &eid, sizeof(uint16_t)); ptr += sizeof(uint16_t);

  enet_peer_send(peer, 0, packet);
}

void send_entity_input(ENetPeer *peer, uint16_t eid, float thr, float steer)
{
  ENetPacket *packet = enet_packet_create(nullptr, sizeof(uint8_t) + sizeof(uint16_t) +
                                                   sizeof(uint8_t),
                                                   ENET_PACKET_FLAG_UNSEQUENCED);
  uint8_t *ptr = packet->data;
  *ptr = E_CLIENT_TO_SERVER_INPUT; ptr += sizeof(uint8_t);
  memcpy(ptr, &eid, sizeof(uint16_t)); ptr += sizeof(uint16_t);
  float4bitsQuantized thrPacked(thr, -1.f, 1.f);
  float4bitsQuantized steerPacked(steer, -1.f, 1.f);
  uint8_t thrSteerPacked = (thrPacked.packedVal << 4) | steerPacked.packedVal;
  memcpy(ptr, &thrSteerPacked, sizeof(uint8_t)); ptr += sizeof(uint8_t);
  /*
  memcpy(ptr, &thrPacked, sizeof(uint8_t)); ptr += sizeof(uint8_t);
  memcpy(ptr, &oriPacked, sizeof(uint8_t)); ptr += sizeof(uint8_t);
  */

  enet_peer_send(peer, 1, packet);
}

void send_snapshot(ENetPeer *peer, const EntityState& ent, size_t lod)
{
  const size_t x_bits     = x_bits_lods[lod];
  const size_t y_bits     = y_bits_lods[lod];
  const size_t ori_bits   = ori_bits_lods[lod];
  const size_t vx_bits    = vx_bits_lods[lod];
  const size_t vy_bits    = vy_bits_lods[lod];
  const size_t omega_bits = omega_bits_lods[lod];

  uint16_t x_packed    = pack_float<uint16_t>(ent.x, -worldSize, worldSize, x_bits);
  uint16_t y_packed    = pack_float<uint16_t>(ent.y, -worldSize, worldSize, y_bits);
  uint8_t ori_packed   = pack_float<uint8_t> (ent.ori, -PI, PI, ori_bits);
  uint8_t vx_packed    = pack_float<uint8_t> (ent.vx, -max_speed, max_speed, vx_bits);
  uint8_t vy_packed    = pack_float<uint8_t> (ent.vy, -max_speed, max_speed, vy_bits);
  uint8_t omega_packed = pack_float<uint8_t> (ent.omega, -max_omega, max_omega, omega_bits);

  ENetPacket *packet = enet_packet_create(nullptr, sizeof(uint8_t) + sizeof(uint16_t) +
                                                   sizeof(uint16_t) +
                                                   sizeof(uint16_t) +
                                                   sizeof(uint8_t) +
                                                   sizeof(uint8_t) +
                                                   sizeof(uint8_t) +
                                                   sizeof(uint8_t) +
                                                   sizeof(uint8_t),
                                                   ENET_PACKET_FLAG_UNSEQUENCED);
  uint8_t *ptr = packet->data;

  *ptr = E_SERVER_TO_CLIENT_SNAPSHOT;           ptr += sizeof(uint8_t);
  memcpy(ptr, &ent.eid,      sizeof(uint16_t)); ptr += sizeof(uint16_t);
  memcpy(ptr, &x_packed,     sizeof(uint16_t)); ptr += sizeof(uint16_t);
  memcpy(ptr, &y_packed,     sizeof(uint16_t)); ptr += sizeof(uint16_t);
  memcpy(ptr, &ori_packed,   sizeof(uint8_t )); ptr += sizeof(uint8_t);
  memcpy(ptr, &vx_packed,    sizeof(uint8_t )); ptr += sizeof(uint8_t);
  memcpy(ptr, &vy_packed,    sizeof(uint8_t )); ptr += sizeof(uint8_t);
  memcpy(ptr, &omega_packed, sizeof(uint8_t )); ptr += sizeof(uint8_t);
  *ptr = lod;                                   ptr += sizeof(uint8_t);
  enet_peer_send(peer, 1, packet);
}

void send_short_snapshot(ENetPeer *peer, const ShortEntityState& ent)
{
  const size_t x_bits     = x_bits_lods[short_snapshot_default_lod];
  const size_t y_bits     = y_bits_lods[short_snapshot_default_lod];
  const size_t ori_bits   = ori_bits_lods[short_snapshot_default_lod];

  uint16_t x_packed    = pack_float<uint16_t>(ent.x, -worldSize, worldSize, x_bits);
  uint16_t y_packed    = pack_float<uint16_t>(ent.y, -worldSize, worldSize, y_bits);
  uint8_t ori_packed   = pack_float<uint8_t> (ent.ori, -PI, PI, ori_bits);

  ENetPacket *packet = enet_packet_create(nullptr, sizeof(uint8_t) + sizeof(uint16_t) +
                                                   sizeof(uint16_t) +
                                                   sizeof(uint16_t) +
                                                   sizeof(uint8_t),
                                                   ENET_PACKET_FLAG_UNSEQUENCED);
  uint8_t *ptr = packet->data;

  *ptr = E_CLIENT_TO_SERVER_SHORT_SNAPSHOT;     ptr += sizeof(uint8_t);
  memcpy(ptr, &ent.eid,      sizeof(uint16_t)); ptr += sizeof(uint16_t);
  memcpy(ptr, &x_packed,     sizeof(uint16_t)); ptr += sizeof(uint16_t);
  memcpy(ptr, &y_packed,     sizeof(uint16_t)); ptr += sizeof(uint16_t);
  memcpy(ptr, &ori_packed,   sizeof(uint8_t )); ptr += sizeof(uint8_t);

  enet_peer_send(peer, 1, packet);
}

void send_ack_snapshot(ENetPeer *peer, uint16_t eid)
{
  ENetPacket *packet = enet_packet_create(nullptr, sizeof(uint8_t) + sizeof(uint16_t), ENET_PACKET_FLAG_UNSEQUENCED);
  uint8_t *ptr = packet->data;
  *ptr = E_CLIENT_TO_SERVER_ACK_SNAPSHOT; ptr += sizeof(uint8_t);
  memcpy(ptr, &eid, sizeof(uint16_t));    ptr += sizeof(uint16_t);

  enet_peer_send(peer, 1, packet);
}

void send_time_msec(ENetPeer *peer, uint32_t timeMsec)
{
  ENetPacket *packet = enet_packet_create(nullptr, sizeof(uint8_t) + sizeof(uint32_t),
                                                   ENET_PACKET_FLAG_RELIABLE);
  uint8_t *ptr = packet->data;
  *ptr = E_SERVER_TO_CLIENT_TIME_MSEC; ptr += sizeof(uint8_t);
  memcpy(ptr, &timeMsec, sizeof(uint32_t)); ptr += sizeof(uint32_t);

  enet_peer_send(peer, 0, packet);
}

MessageType get_packet_type(ENetPacket *packet)
{
  return (MessageType)*packet->data;
}

void deserialize_new_entity(ENetPacket *packet, Entity &ent)
{
  uint8_t *ptr = packet->data; ptr += sizeof(uint8_t);
  ent = *(Entity*)(ptr); ptr += sizeof(Entity);
}

void deserialize_set_controlled_entity(ENetPacket *packet, uint16_t &eid)
{
  uint8_t *ptr = packet->data; ptr += sizeof(uint8_t);
  eid = *(uint16_t*)(ptr); ptr += sizeof(uint16_t);
}

void deserialize_entity_input(ENetPacket *packet, uint16_t &eid, float &thr, float &steer)
{
  uint8_t *ptr = packet->data; ptr += sizeof(uint8_t);
  eid = *(uint16_t*)(ptr); ptr += sizeof(uint16_t);
  uint8_t thrSteerPacked = *(uint8_t*)(ptr); ptr += sizeof(uint8_t);
  /*
  uint8_t thrPacked = *(uint8_t*)(ptr); ptr += sizeof(uint8_t);
  uint8_t oriPacked = *(uint8_t*)(ptr); ptr += sizeof(uint8_t);
  */
  static uint8_t neutralPackedValue = pack_float<uint8_t>(0.f, -1.f, 1.f, 4);
  static uint8_t nominalPackedValue = pack_float<uint8_t>(1.f, 0.f, 1.2f, 4);
  float4bitsQuantized thrPacked(thrSteerPacked >> 4);
  float4bitsQuantized steerPacked(thrSteerPacked & 0x0f);
  thr = thrPacked.packedVal == neutralPackedValue ? 0.f : thrPacked.unpack(-1.f, 1.f);
  steer = steerPacked.packedVal == neutralPackedValue ? 0.f : steerPacked.unpack(-1.f, 1.f);
}

void deserialize_snapshot(ENetPacket *packet, EntityState& ent)
{
  uint8_t *ptr = packet->data;
  ptr += sizeof(uint8_t);

           ent.eid      = *reinterpret_cast<uint16_t *>(ptr); ptr += sizeof(uint16_t);
  uint16_t x_packed     = *reinterpret_cast<uint16_t *>(ptr); ptr += sizeof(uint16_t);
  uint16_t y_packed     = *reinterpret_cast<uint16_t *>(ptr); ptr += sizeof(uint16_t);
  uint8_t  ori_packed   = *reinterpret_cast<uint8_t  *>(ptr); ptr += sizeof(uint8_t);
  uint8_t  vx_packed    = *reinterpret_cast<uint8_t  *>(ptr); ptr += sizeof(uint8_t);
  uint8_t  vy_packed    = *reinterpret_cast<uint8_t  *>(ptr); ptr += sizeof(uint8_t);
  uint8_t  omega_packed = *reinterpret_cast<uint8_t  *>(ptr); ptr += sizeof(uint8_t);
  uint8_t  lod          = *reinterpret_cast<uint8_t  *>(ptr); ptr += sizeof(uint8_t);

  const size_t x_bits     = x_bits_lods[lod];
  const size_t y_bits     = y_bits_lods[lod];
  const size_t ori_bits   = ori_bits_lods[lod];
  const size_t vx_bits    = vx_bits_lods[lod];
  const size_t vy_bits    = vy_bits_lods[lod];
  const size_t omega_bits = omega_bits_lods[lod];

  ent.x     = unpack_float(x_packed, -worldSize, worldSize, x_bits);
  ent.y     = unpack_float(y_packed, -worldSize, worldSize, y_bits);
  ent.ori   = unpack_float(ori_packed, -PI, PI, ori_bits);
  ent.vx    = unpack_float(vx_packed, -max_speed, max_speed, vx_bits);
  ent.vy    = unpack_float(vy_packed, -max_speed, max_speed, vy_bits);
  ent.omega = unpack_float(omega_packed, -max_omega, max_omega, omega_bits);
}

void deserialize_short_snapshot(ENetPacket *packet, ShortEntityState& ent)
{
  const size_t x_bits     = x_bits_lods[short_snapshot_default_lod];
  const size_t y_bits     = y_bits_lods[short_snapshot_default_lod];
  const size_t ori_bits   = ori_bits_lods[short_snapshot_default_lod];

  uint8_t *ptr = packet->data;
  ptr += sizeof(uint8_t);

           ent.eid      = *reinterpret_cast<uint16_t *>(ptr); ptr += sizeof(uint16_t);
  uint16_t x_packed     = *reinterpret_cast<uint16_t *>(ptr); ptr += sizeof(uint16_t);
  uint16_t y_packed     = *reinterpret_cast<uint16_t *>(ptr); ptr += sizeof(uint16_t);
  uint8_t  ori_packed   = *reinterpret_cast<uint8_t  *>(ptr); ptr += sizeof(uint8_t);

  ent.x     = unpack_float(x_packed, -worldSize, worldSize, x_bits);
  ent.y     = unpack_float(y_packed, -worldSize, worldSize, y_bits);
  ent.ori   = unpack_float(ori_packed, -PI, PI, ori_bits);
}

void deserialize_ack_snapshot(ENetPacket *packet, uint16_t &eid)
{
  uint8_t *ptr = packet->data;
  ptr += sizeof(uint8_t);

  eid = *reinterpret_cast<uint16_t *>(ptr); ptr += sizeof(uint16_t);
}

void deserialize_time_msec(ENetPacket *packet, uint32_t &timeMsec)
{
  uint8_t *ptr = packet->data; ptr += sizeof(uint8_t);
  timeMsec = *(uint32_t*)(ptr); ptr += sizeof(uint32_t);
}

