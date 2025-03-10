#pragma once

#include <iostream>

constexpr size_t Max_connections = 16;
constexpr size_t Lobby_port = 10887;

static constexpr size_t Max_name_length = 20;
static const char *Player_names[Max_connections]
{
    "Radomir",
    "Kamala",
    "Jensen",
    "Kratos",
    "Christian",
    "Kylian",
    "Jenniffer",
    "Travers",
    "Ramsey",
    "Earnest",
    "Jaden",
    "Felicia",
    "Mitchell",
    "Jessi",
    "Lora",
    "Isobel"
};

//---------------------------------
// Structures for messages
//---------------------------------
struct PlayerInfo
{
    size_t id;
    char name[Max_name_length];
};

struct PlayerUpdatePacket
{
    size_t id;
    size_t ping;
    float pos_x;
    float pos_y;
};

//---------------------------------

enum GameMsgType
{
    NewConnection,
    PlayersInfo,
    PlayersUpdate
};

struct GameMsg
{
    GameMsgType type;
    char payload[];
};

struct NewConnectionMsg
{
    PlayerInfo info;
};

struct PlayersInfoMsg
{
    size_t receiver_id;
    size_t players_count;
    PlayerInfo players[];
};

struct PlayersUpdateMsg
{
    size_t players_count;
    PlayerUpdatePacket players[];
};
