#pragma once
#include <cstdint>
#include <enet/enet.h>
#include "entity.h"

#include <queue>
#include <limits.h>

enum MessageType : uint8_t
{
  E_CLIENT_TO_SERVER_JOIN = 0,
  E_SERVER_TO_CLIENT_NEW_ENTITY,
  E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY,
  E_CLIENT_TO_SERVER_STATE,
  E_SERVER_TO_CLIENT_SNAPSHOT
};

void send_join(ENetPeer *peer);
void send_new_entity(ENetPeer *peer, const Entity &ent);
void send_set_controlled_entity(ENetPeer *peer, uint16_t eid);
void send_entity_state(ENetPeer *peer, uint16_t eid, float x, float y);
void send_snapshot(ENetPeer *peer, uint16_t eid, float x, float y, float score);

MessageType get_packet_type(ENetPacket *packet);

void deserialize_new_entity(ENetPacket *packet, Entity &ent);
void deserialize_set_controlled_entity(ENetPacket *packet, uint16_t &eid);
void deserialize_entity_state(ENetPacket *packet, uint16_t &eid, float &x, float &y);
void deserialize_snapshot(ENetPacket *packet, uint16_t &eid, float &x, float &y, float &score);

//-----------------------------------------------------------------------------------------

#include <iostream>
class BitStream 
{
  public:
    BitStream(size_t reserved_size = sizeof(Entity)):
      data(),
      cur_byte(0)
    {
      data.reserve(reserved_size);
    }

    void *get_bytes(size_t &size)
    {
      size = data.size() - cur_byte;
      return data.data() + cur_byte;
    }

    template <typename T>
    void write(const T& value)
    {
      const uint8_t* byte_ptr = reinterpret_cast<const uint8_t*>(&value);
      for (size_t i = 0; i < sizeof(T); ++i)
        data.push_back(byte_ptr[i]);
    }

    template <typename T>
    T read()
    {
      T value = 0;
      uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(&value);

      for (size_t i = 0; i < sizeof(T); ++i)
        byte_ptr[i] = data[cur_byte++];

      if (cur_byte > Cur_byte_value_to_reallocate)
      {
        if (data.size() < cur_byte)
        {
          std::cerr << "Error when optimizing bitstream place.\n";
          cur_byte = 0;
          data.clear();
          return value;
        }

        size_t real_size = data.size() - cur_byte;
        for (size_t i = 0; i < real_size; ++i)
          data[i] = data[cur_byte + i];

        cur_byte = 0;
        data.resize(real_size);
      }

      return value;
    }

    template <typename T>
    void read(T &value)
    {
      value = read<T>();
    }

    template <typename T>
    BitStream& operator<<(const T& value)
    {
      write(value);
      return *this;
    }

    template <typename T>
    BitStream& operator>>(T& value)
    {
      value = read<T>();
      return *this;
    }

  private:
    std::vector<uint8_t> data;
    size_t cur_byte = 0;

    static constexpr size_t Cur_byte_value_to_reallocate = 100;
};