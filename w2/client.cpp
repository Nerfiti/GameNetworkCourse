#include "raylib.h"
#include <enet/enet.h>
#include <iostream>
#include <cstring>
#include <map>
#include <thread>

#include "general.h"

//----------------------------------------------------------
// Some data types
//----------------------------------------------------------

struct player_info_t
{
  std::string name;
  float pos_x;
  float pos_y;
  size_t ping;
};

using players_map = std::map<size_t, player_info_t>;

//----------------------------------------------------------

static players_map players;
static size_t my_id = std::numeric_limits<size_t>::max();

//----------------------------------------------------------

static bool process_packet_from_lobby(ENetPacket *packet);

static void server_thread_func(ENetHost *client, ENetAddress address);
static void process_packet_from_server(ENetPacket *packet);
static ENetPacket *create_update_packet();

//----------------------------------------------------------

int main(int argc, const char **argv)
{
  //----------------------------------------------------------

  int width = 800;
  int height = 600;
  InitWindow(width, height, "w2 MIPT networked");

  const int scrWidth = GetMonitorWidth(0);
  const int scrHeight = GetMonitorHeight(0);
  if (scrWidth < width || scrHeight < height)
  {
    width = std::min(scrWidth, width);
    height = std::min(scrHeight - 150, height);
    SetWindowSize(width, height);
  }

  SetTargetFPS(60);

  //----------------------------------------------------------

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
  address.port = Lobby_port;

  ENetPeer *lobbyPeer = enet_host_connect(client, &address, 2, 0);
  if (!lobbyPeer)
  {
    printf("Cannot connect to lobby");
    return 1;
  }

  //----------------------------------------------------------

  bool connected_to_lobby = false;
  float posx = GetRandomValue(0, width);
  float posy = GetRandomValue(0, height);
  float velx = 0.0f;
  float vely = 0.0f;

  //----------------------------------------------------------

  while (!WindowShouldClose())
  {
    //----------------------------------------------------------

    const float dt = GetFrameTime();
    ENetEvent event;
    while (enet_host_service(client, &event, 10) > 0)
    {
      switch (event.type)
      {
        case ENET_EVENT_TYPE_CONNECT:
        {
          printf("LOBBY: Connection with %x:%u established\n", event.peer->address.host, event.peer->address.port);
          connected_to_lobby = true;
          break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
        {
          std::string recv_packet = std::string(reinterpret_cast<char *>(event.packet->data), event.packet->dataLength) + "\0";
          printf("LOBBY: Packet received '%s'\n", recv_packet.c_str());
          process_packet_from_lobby(event.packet);
          enet_packet_destroy(event.packet);
          break;
        }
        default:
          break;
      };
    }

    if (connected_to_lobby && my_id == std::numeric_limits<size_t>::max() && IsKeyDown(KEY_ENTER))
    {
      std::string start_command = "START";
      ENetPacket *packet = enet_packet_create(start_command.c_str(), start_command.size() + 1, ENET_PACKET_FLAG_RELIABLE);
      enet_peer_send(lobbyPeer, 0, packet);
    }

    //----------------------------------------------------------

    bool left = IsKeyDown(KEY_LEFT);
    bool right = IsKeyDown(KEY_RIGHT);
    bool up = IsKeyDown(KEY_UP);
    bool down = IsKeyDown(KEY_DOWN);
    constexpr float accel = 30.f;
    velx += ((left ? -1.f : 0.f) + (right ? 1.f : 0.f)) * dt * accel;
    vely += ((up ? -1.f : 0.f) + (down ? 1.f : 0.f)) * dt * accel;
    posx += velx * dt;
    posy += vely * dt;
    velx *= 0.99f;
    vely *= 0.99f;

    //----------------------------------------------------------

    if (my_id == std::numeric_limits<size_t>::max())
    {
      BeginDrawing();
        ClearBackground(BLACK);
        DrawText(TextFormat("Current status: %s", "unknown"), 20, 20, 20, WHITE);
        DrawText(TextFormat("My position: (%d, %d)", (int)posx, (int)posy), 20, 40, 20, WHITE);
        DrawText("List of players:", 20, 60, 20, WHITE);
        DrawCircleV(Vector2{posx, posy}, 10.f, WHITE);
      EndDrawing();
    }
    else
    {
      players[my_id].pos_x = posx;
      players[my_id].pos_y = posy;
      size_t text_pos_y = 0;
      BeginDrawing();
        ClearBackground(BLACK);
        DrawText(TextFormat("Current status: %s", "connected"), 20, text_pos_y += 20, 20, WHITE);
        DrawText(TextFormat("My position: (%d, %d)", (int)posx, (int)posy), 20, text_pos_y += 20, 20, WHITE);
        DrawText("List of players:", 20, text_pos_y += 20, 20, WHITE);

        for (auto &[_, info] : players)
          DrawText(TextFormat("Name: %s. Ping: %zu", info.name.c_str(), info.ping), 20, text_pos_y += 20, 20, WHITE);

        for (auto &[_, info] : players)
          DrawCircleV(Vector2{info.pos_x, info.pos_y}, 10.f, WHITE);

      EndDrawing();
    }

    //----------------------------------------------------------
  }

  enet_host_destroy(client);
  enet_deinitialize();
  return 0;
}

static bool process_packet_from_lobby(ENetPacket *packet)
{
  //----------------------------------------------------------

  size_t server_port = 0;
  size_t server_port_msg_prefix_len = 13;
  if (!strncmp(reinterpret_cast<char *>(packet->data), "SERVER PORT: ", 13))
    server_port = std::stol(reinterpret_cast<char *>(packet->data + server_port_msg_prefix_len));
  else
    return false;

  //----------------------------------------------------------

  printf("Got server port (%zu) from lobby. Start connection.\n", server_port);

  ENetHost *client = enet_host_create(nullptr, 1, 2, 0, 0);
  if (!client)
  {
    printf("Cannot create ENet client\n");
    return 1;
  }

  ENetAddress address;
  enet_address_set_host(&address, "localhost");
  address.port = server_port;

  std::thread(server_thread_func, client, address).detach();
  return true;

  //----------------------------------------------------------
}

static void server_thread_func(ENetHost *client, ENetAddress address)
{
  //----------------------------------------------------------

  ENetPeer *serverPeer = enet_host_connect(client, &address, 2, 0);
  if (!serverPeer)
  {
    printf("Cannot connect to server\n");
    return;
  }

  //----------------------------------------------------------

  while (true)
  {
    ENetEvent event;
    constexpr size_t timeout = 10;
    while (enet_host_service(client, &event, timeout) > 0)
    {
      switch (event.type)
      {
        case ENET_EVENT_TYPE_CONNECT:
        {
          printf("Connection with %x:%u established\n", event.peer->address.host, event.peer->address.port);
          break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
        {
          process_packet_from_server(event.packet);
          enet_packet_destroy(event.packet);
          break;
        }
        default:
          break;
      };
    }
    enet_peer_send(serverPeer, 0, create_update_packet());
  }
}

static void process_packet_from_server(ENetPacket *packet)
{
  GameMsg *msg = reinterpret_cast<GameMsg *>(packet->data);
  switch (msg->type)
  {
    case NewConnection:
    {
      NewConnectionMsg *payload = reinterpret_cast<NewConnectionMsg *>(msg->payload);
      players.emplace(payload->info.id, payload->info.name);
      break;
    }
    case PlayersInfo:
    {
      PlayersInfoMsg *payload = reinterpret_cast<PlayersInfoMsg *>(msg->payload);
      for (size_t i = 0; i < payload->players_count; ++i)
      {
        players[payload->players[i].id] = {};
        players[payload->players[i].id].name = payload->players[i].name;
      }
      my_id = payload->receiver_id;
      break;
    }
    case PlayersUpdate:
    {
      PlayersUpdateMsg *payload = reinterpret_cast<PlayersUpdateMsg *>(msg->payload);
      for (size_t i = 0; i < payload->players_count; ++i)
      {
        players[payload->players[i].id].ping = payload->players[i].ping;

        if (payload->players[i].id == my_id) [[unlikely]]
          continue;
        players[payload->players[i].id].pos_x = payload->players[i].pos_x;
        players[payload->players[i].id].pos_y = payload->players[i].pos_y;
      }
      break;
    }
    default:
      break;
  }
}

static ENetPacket *create_update_packet()
{
  PlayerUpdatePacket packet;
  packet.id = my_id;
  packet.pos_x = players[my_id].pos_x;
  packet.pos_y = players[my_id].pos_y;

  return enet_packet_create(&packet, sizeof(packet), ENET_PACKET_FLAG_UNSEQUENCED);
}