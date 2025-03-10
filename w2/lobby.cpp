#include <cstring>
#include <enet/enet.h>
#include <iostream>
#include <list>
#include <map>
#include <set>

#include "general.h"

constexpr size_t Server_port = 9999;

//----------------------------------------------------------
// Some data types
//----------------------------------------------------------
struct player_info_t
{
  float pos_x = 0;
  float pos_y = 0;
  ENetPeer *peer;
};

using players_info_t = std::map<size_t, player_info_t>;

//----------------------------------------------------------

static std::set<size_t> create_set_range(size_t begin, size_t end);

static void process_packet(bool is_lobby, ENetPeer *peer, ENetPacket *packet = nullptr);
static void process_packet_as_lobby(ENetPeer *peer, ENetPacket *packet = nullptr);
static void process_packet_as_server(ENetPeer *peer, ENetPacket *packet = nullptr);

static GameMsg *create_new_connection_msg(size_t id, const std::string &name, size_t &msg_size);
static GameMsg *create_players_info_msg(size_t id, const players_info_t &players, size_t &msg_size);
static GameMsg *create_players_update_msg(const players_info_t &players, size_t &msg_size);

//----------------------------------------------------------

int main(int argc, const char **argv)
{

  //----------------------------------------------------------

  if (enet_initialize() != 0)
  {
    printf("Cannot init ENet");
    return 1;
  }

  bool is_lobby = true;
  if (argc > 1 && (!strcmp(argv[1], "-g") || !strcmp(argv[1], "--game_server")))
    is_lobby = false;

  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = is_lobby ? Lobby_port : Server_port;

  constexpr size_t Channel_limit = 2;
  ENetHost *server = enet_host_create(&address, Max_connections, Channel_limit, 0, 0);
  if (!server)
  {
    printf("Cannot create ENet server\n");
    return 1;
  }

  //----------------------------------------------------------

  while (true)
  {
    ENetEvent event;
    constexpr size_t timeout = 10;
    while (enet_host_service(server, &event, timeout) > 0)
    {
      switch (event.type)
      {
        case ENET_EVENT_TYPE_CONNECT:
        {
          printf("Connection with %x:%u established\n", event.peer->address.host, event.peer->address.port);
          process_packet(is_lobby, event.peer);
          break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
        {
          process_packet(is_lobby, event.peer, event.packet);
          enet_packet_destroy(event.packet);
          break;
        }
        default:
          break;
        };
    }
  }

  enet_host_destroy(server);
  atexit(enet_deinitialize);
  return 0;
}

//----------------------------------------------------------

static void process_packet(bool is_lobby, ENetPeer *peer, ENetPacket *packet)
{
  if (is_lobby)
    process_packet_as_lobby(peer, packet);
  else
    process_packet_as_server(peer, packet);
}

static void process_packet_as_lobby(ENetPeer *peer, ENetPacket *packet)
{
  static std::string start_session_msg = "SERVER PORT: " + std::to_string(Server_port);
  ENetPacket *start_session_packet = enet_packet_create(start_session_msg.c_str(), start_session_msg.size() + 1, ENET_PACKET_FLAG_RELIABLE);

  static bool session_started = false;
  static std::list<ENetPeer *> players;

  if (!session_started && packet && !strcmp(reinterpret_cast<char *>(packet->data), "START"))
  {
    for (auto peer : players)
      enet_peer_send(peer, 0, start_session_packet);

    players.clear();

    session_started = true;

    printf("Session was started by player %x:%u\n", peer->address.host, peer->address.port);
    return;
  }

  if (session_started)
  {
    printf("Connecting after the start of the session. Sending the server port...\n");
    enet_peer_send(peer, 0, start_session_packet);
  }
  else
  {
    printf("New player is waiting for the game: %x:%u\n", peer->address.host, peer->address.port);
    players.push_back(peer);
  }
}

static void process_packet_as_server(ENetPeer *peer, ENetPacket *packet)
{
  static bool initialized = false;
  static players_info_t players;
  static std::set<size_t> free_ids = create_set_range(0, Max_connections);

  GameMsg *msg = nullptr;
  size_t msg_size = 0;

  if (packet)
  {
    PlayerUpdatePacket *pos = reinterpret_cast<PlayerUpdatePacket *>(packet->data);
    players[pos->id].pos_x = pos->pos_x;
    players[pos->id].pos_y = pos->pos_y;
    players[pos->id].peer = peer;
  }
  else
  {
    printf("New connection: %x:%u\n", peer->address.host, peer->address.port);
    if (free_ids.empty())
    {
      printf("Server is full!");
      return;
    }
    size_t id = *free_ids.begin();

    if (!players.empty())
    {
      msg = create_new_connection_msg(id, Player_names[id], msg_size);
      for (auto &[_, info] : players)
      {
        enet_peer_send(info.peer, 0, enet_packet_create(msg, msg_size, ENET_PACKET_FLAG_RELIABLE));
      }
      free(msg);
      printf("All users was notified about new player!\n");
    }

    free_ids.erase(id);
    player_info_t info;
    info.peer = peer;
    info.pos_x = 0;
    info.pos_y = 0;
    players.emplace(id, info);

    msg = create_players_info_msg(id, players, msg_size);
    enet_peer_send(peer, 0, enet_packet_create(msg, msg_size, ENET_PACKET_FLAG_RELIABLE));
    free(msg);

    printf("Information about all users was sended to the new player.\n");
  }

  msg = create_players_update_msg(players, msg_size);
  for (auto &[_, info] : players)
  {
    enet_peer_send(info.peer, 0, enet_packet_create(msg, msg_size, ENET_PACKET_FLAG_UNSEQUENCED));
  }
  free(msg);
}

//----------------------------------------------------------

static GameMsg *create_new_connection_msg(size_t id, const std::string &name, size_t &msg_size)
{
  msg_size = sizeof(GameMsg) + sizeof(NewConnectionMsg);
  GameMsg *msg = reinterpret_cast<GameMsg *>(calloc(1, msg_size));
  msg->type = NewConnection;
  NewConnectionMsg *payload = reinterpret_cast<NewConnectionMsg *>(msg->payload);
  payload->info.id = id;
  memset(payload->info.name, 0, Max_name_length);
  strncpy(payload->info.name, Player_names[id], Max_name_length - 1);

  return msg;
}

static GameMsg *create_players_info_msg(size_t id, const players_info_t &players, size_t &msg_size)
{
  msg_size = sizeof(GameMsg) + sizeof(PlayersInfoMsg) + players.size() * sizeof(PlayerInfo);
  GameMsg *msg = reinterpret_cast<GameMsg *>(calloc(1, msg_size));
  msg->type = PlayersInfo;
  PlayersInfoMsg *payload = reinterpret_cast<PlayersInfoMsg *>(msg->payload);
  payload->receiver_id = id;
  payload->players_count = players.size();

  size_t i = 0;
  for (auto &[player_id, info] : players)
  {
    payload->players[i].id = player_id;
    memset(payload->players[i].name, 0, Max_name_length);
    strncpy(payload->players[i].name, Player_names[player_id], Max_name_length - 1);
    ++i;
  }

  return msg;
}

static GameMsg *create_players_update_msg(const players_info_t &players, size_t &msg_size)
{
  msg_size = sizeof(GameMsg) + sizeof(PlayersUpdateMsg) + players.size() * sizeof(PlayerUpdatePacket);
  GameMsg *msg = reinterpret_cast<GameMsg *>(calloc(1, msg_size));
  msg->type = PlayersUpdate;
  PlayersUpdateMsg *payload = reinterpret_cast<PlayersUpdateMsg *>(msg->payload);
  payload->players_count = players.size();

  size_t i = 0;
  for (auto &[player_id, info] : players)
  {
    payload->players[i].id = player_id;
    payload->players[i].ping = info.peer->roundTripTime;
    payload->players[i].pos_x = info.pos_x;
    payload->players[i].pos_y = info.pos_y;
    ++i;
  }

  return msg;
}

//----------------------------------------------------------

static std::set<size_t> create_set_range(size_t begin, size_t end)
{
  std::set<size_t> set;
  for (size_t i = begin; i < end; ++i)
    set.insert(i);

  return set;
}