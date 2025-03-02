#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <list>
#include "socket_tools.h"

struct userID_t final
{
  sockaddr_in addr;
  socklen_t slen;
  std::string name;

  userID_t() = default;
  userID_t(sockaddr_in address, socklen_t len, std::string username):
    addr(address),
    slen(len),
    name(username)
  {}

  bool operator==(const userID_t other)
  {
    return addr.sin_addr.s_addr == other.addr.sin_addr.s_addr 
            && addr.sin_family == other.addr.sin_family
            && addr.sin_port == other.addr.sin_port
            && slen == other.slen;
  }
};

using user_list_t = std::list<userID_t>;

static void process_message(const char *buffer, size_t buf_size, int sfd, userID_t uid);
static void register_client(std::string name, int sfd, userID_t uid, user_list_t &users);
static void process_duel(int sfd, userID_t uid, const user_list_t &users, bool is_answer, std::string answer = "");

static std::string generate_problem(size_t &correct_answer);

static void send_message(const std::string &msg, int sfd, userID_t uid);
static void notify_all_users(const std::string &msg, int sfd, user_list_t users);

int main(int argc, const char **argv)
{
  const char *port = "2025";

  int sfd = create_dgram_socket(nullptr, port, nullptr);

  if (sfd == -1)
  {
    printf("cannot create socket\n");
    return 1;
  }
  printf("listening!\n");

  while (true)
  {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(sfd, &readSet);

    timeval timeout = { 0, 100000 }; // 100 ms
    select(sfd + 1, &readSet, NULL, NULL, &timeout);

    if (FD_ISSET(sfd, &readSet))
    {
      constexpr size_t buf_size = 1000;
      static char buffer[buf_size];
      memset(buffer, 0, buf_size);

      sockaddr_in sin;
      socklen_t slen = sizeof(sockaddr_in);
      ssize_t numBytes = recvfrom(sfd, buffer, buf_size - 1, 0, (sockaddr*)&sin, &slen);
      if (numBytes > 0)
      {
        printf("(%s:%d) %s\n", inet_ntoa(sin.sin_addr), sin.sin_port, buffer); // assume that buffer is a string

        process_message(buffer, numBytes, sfd, userID_t(sin, slen, ""));
      }
    }
  }
  return 0;
}

static void process_message(const char *buffer, size_t buf_size, int sfd, userID_t uid)
{
  constexpr size_t bucket_count = 5;
  static user_list_t users;

  if (auto it = std::find(users.begin(), users.end(), uid); it != users.end())
    uid.name = it->name;

  constexpr size_t connect_prefix_len = 3;
  if (buf_size > connect_prefix_len && !strncmp(buffer, "/c ", connect_prefix_len))
  {
    register_client(std::string(buffer + connect_prefix_len, buf_size - connect_prefix_len), sfd, uid, users);
    return;
  }

  constexpr size_t start_duel_prefix_len = 9;
  if (buf_size >= start_duel_prefix_len && !strncmp(buffer, "/mathduel", start_duel_prefix_len))
  {
    process_duel(sfd, uid, users, false);
    return;
  }

  constexpr size_t duel_answer_prefix_len = 5;
  if (buf_size > duel_answer_prefix_len && !strncmp(buffer, "/ans ", duel_answer_prefix_len))
  {
    process_duel(sfd, uid, users, true, std::string(buffer + duel_answer_prefix_len, buf_size - duel_answer_prefix_len));
    return;
  }
}

static void register_client(std::string name, int sfd, userID_t uid, user_list_t &users)
{
  if (std::find(users.begin(), users.end(), uid) != users.end())
  {
    send_message("You are already connected!", sfd, uid);
    return;
  }

  if (!name.empty())
  {
    for (auto& uid : users)
      send_message(name + " connected to the server.", sfd, uid);

    users.emplace_back(uid.addr, uid.slen, name);
    send_message("You successfully connected to the server", sfd, uid);
  }
  else
  {
    send_message("Type your name, please", sfd, uid);
  }
}

static void process_duel(int sfd, userID_t uid, const user_list_t &users, bool is_answer, std::string answer)
{
  enum class battle_state
  {
    IDLE,
    WAITING_SECOND_PLAYER,
    IN_PROCESS
  };
  static battle_state state = battle_state::IDLE;
  static size_t correct_answer = 0;
  static userID_t first_player;
  static userID_t second_player;

  switch (state)
  {
    case battle_state::IDLE:
    {
      if (is_answer)
      {
        send_message("Duel is not in process now!", sfd, uid);
        return;
      }

      first_player = uid;
      state = battle_state::WAITING_SECOND_PLAYER;
      notify_all_users(uid.name + " request for math duel.", sfd, users);
      break;
    }
    case battle_state::WAITING_SECOND_PLAYER:
    {
      if (is_answer)
      {
        send_message("Duel is not in process now!", sfd, uid);
        return;
      }

      second_player = uid;
      state = battle_state::IN_PROCESS;
      notify_all_users(uid.name + " accepted the challenge. The problem is: " + generate_problem(correct_answer), sfd, users);
    }
    case battle_state::IN_PROCESS:
    {
      if (!is_answer)
      {
        send_message("Wait for current duel to end.", sfd, uid);
        return;
      }

      size_t user_answer = std::stoull(answer);
      notify_all_users(uid.name + " answers " + std::to_string(user_answer), sfd, users);

      if (user_answer == correct_answer)
      {
        notify_all_users("This is a correct answer. " + uid.name + " won this duel. Congratulations!", sfd, users);
        state = battle_state::IDLE;
      }
      else
      {
        notify_all_users("This is an incorrect answer.", sfd, users);
      }
    }
  }
}

static void send_message(const std::string &msg, int sfd, userID_t uid)
{
  if (sendto(sfd, msg.c_str(), msg.size(), 0, reinterpret_cast<sockaddr *>(&uid.addr), uid.slen) == -1)
    std::cerr << strerror(errno) << std::endl;
}

static void notify_all_users(const std::string &msg, int sfd, user_list_t users)
{
  for (const auto &user : users)
    send_message(msg, sfd, user);
}

static std::string generate_problem(size_t &correct_answer)
{
  size_t a = rand() % 1000;
  size_t b = rand() % 1000;
  size_t c = rand() % 1000;

  size_t op = rand() % 2;
  if (op == 0)
  {
    correct_answer = a * b + c;
    return std::to_string(a) + "*" + std::to_string(b) + "+" + std::to_string(c);
  }

  while (c > a * b)
    c = a * b;

  correct_answer = a * b - c;
  return std::to_string(a) + "*" + std::to_string(b) + "-" + std::to_string(c);
}