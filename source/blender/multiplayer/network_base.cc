#include "network_base.hh"

/* Networking classes */
#include <iphlpapi.h> /* For retrieving private IP */
#include <windows.h>
#include <ws2tcpip.h>

#include <chrono>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")

/*
 * ---------------------------------------------------------------------------------------
 *  Code was made using help from https://beej.us/guide/bgnet/pdf/bgnet_usl_c_1.pdf.
 * ---------------------------------------------------------------------------------------
 */

Dance::~Dance()
{
  /* If still connections, tell that we disconnected. */
  const char *message = "Disconnect";
  for (const auto &value : them_addrss_.values()) {
    send_message(message, int(strlen(message)), value, false, nullptr);
  }

  /* Close socket so that recvfrom() will end. */
  shutdown(sockfd_, SD_BOTH);
  closesocket(sockfd_);
  /* Wait for thread. */
  if (connection_made_.valid()) {
    quit_listening_ = true;
    quit_callback_ = true;
    connection_made_.get(); /* Waits for the thread to finish execution. */
  }
  if (listen_thread_.joinable()) {
    quit_listening_ = true;
    listen_thread_.join();
  }
  if (callback_thread_.joinable()) {
    quit_callback_ = true;
    callback_thread_.join();
  }
  them_numbers_.clear();
  them_addrss_.clear();
}

void Dance::MU_init(bool use_callback, bool force_IPV4)
{
  /* First pass the version of winsock
   * Latest version is 2.2, this one we want to use. */
  WSADATA wsa_data;

  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    /* LOGLINE(DLogObj, DanceLogger::DANCE_CRITICAL, "WSAStartup failed"); */
    exit(1);
  }
  if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2) {
    WSACleanup(); /* We are done with winsock */
    /* LOGLINE(DLogObj, DanceLogger::DANCE_CRITICAL, "Version 2.2 of winsock is not available"); */
    exit(1);
  }

  use_callback_functions_ = use_callback;
  force_IPV4_ = force_IPV4;
}

/* -------------------------------------------------------------------- */
/** \Connecting device to eachother
 * \{ */

void Dance::MU_host(Dance::dance_moves moves,
                    int _max_connections,
                    const char *port,
                    bool _force_IPV4,
                    const blender::Vector<std::string> &clients_IP)
{

  /* -------------------------------------------------------------------- */
  /** \Set variables
   * \{ */

  force_IPV4_ = _force_IPV4;
  current_moves_ = moves;
  is_host_ = true;
  max_connections_ = _max_connections;

  /* Check if the inserted port is valid, otherwise just use the host_port. */
  int test_port = 0;
  if (port != NULL) {
    test_port = atoi(port);
  }
  if (test_port <= 1024 || test_port >= 49151) {
    port = host_port_;
    char error_buffer[256];
    sprintf_s(error_buffer, sizeof(error_buffer), "Default port: %s is used", host_port_);
    /* LOGLINE(DLogObj, DanceLogger::DANCE_INFO, error_buffer); */
  }
  else {
    memcpy_s(host_port_, sizeof(host_port_), port, strlen(port));
  }

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Set IPs
   * \{ */

  char other_IP[39] = " ";
  char own_IP[39] = " ";

  /* If we are on the same device, we just use 'localhost' as our IP. */
  if (moves == SAMEDEVICE) {
    /* Use strcpy_s to set other_IP to localhost. */
    const char *same_device_input = "localhost";
    strcpy_s(other_IP, same_device_input);
    strcpy_s(own_IP, same_device_input);
    ipv_ = AF_INET;
  }
  else {
    const bool p = moves == PUBLIC ? true : false;
    strcpy_s(own_IP, MU_get_IP(p).c_str()); /* Set own_IP using MU_get_IP(). */
  }

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Important variables
   * \{ */

  struct addrinfo hints, *res;
  int status;
  const char yes = '1';

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Add clients IP to connections
   * \{ */

  if (moves == PUBLIC) {
    /* Put all the clients IPs in the map. */
    for (int i = 0; i < int(clients_IP.size()); i++) {
      strcpy_s(other_IP, clients_IP[i].c_str());

      memset(&hints, 0, sizeof hints);
      hints.ai_family = ipv_; /* Use #IPV4 or #IPV6. */
      hints.ai_socktype = SOCK_DGRAM;
      /* #addrinfo */
      if ((status = getaddrinfo(other_IP, host_port_, &hints, &res)) != 0) {
        char error_buffer[256];
        sprintf_s(error_buffer,
                  sizeof(error_buffer),
                  "(Invalid IP) getaddrinfo error on clients IP %s",
                  get_send_errors(WSAGetLastError()));
        /* LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
        continue;
      }
      /* Get name of ourself. */
      char info_host[256];
      char info_service[256];
      /* TODO: if IP is wrong this can take ages (sometimes multiple seconds). */
      getnameinfo(res->ai_addr,
                  int(res->ai_addrlen),
                  info_host,
                  sizeof(info_host),
                  info_service,
                  sizeof(info_service),
                  0);

      /* Add host to map. */
      them_addrss_.add(info_host, res);
      them_numbers_.add(info_host, 0);
      /* Don't Increase total connections (We do this after we confirmed the connection in
       * holepunching). */
      /* total_connections_++; */
    }
  }

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Winsock2 code
   * \{ */

  /* Get Address information for this device. */
  res = nullptr;
  memset(&hints, 0, sizeof hints); /* Reset #hints. */
  hints.ai_family = ipv_;          /* Use #IPV4 or #IPV6. */
  hints.ai_socktype = SOCK_DGRAM;  /* Use UPD. */
  /* If we are binding using ipv4, we use our private IP (TODO: needed?). */
  char own_IP2[65];
  if (moves == PUBLIC && ipv_ == AF_INET) {
    strcpy_s(own_IP2, MU_get_IP(false).c_str());
  }
  else {
    strcpy_s(own_IP2, own_IP);
  }

  /* Get address info of the host (ourself). */
  if ((status = getaddrinfo(own_IP, host_port_, &hints, &res)) != 0) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "getaddrinfo error: %s",
              get_send_errors(WSAGetLastError()));
    /* LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    return;
  }

  /* Get name of ourself. */
  char info_host[256];
  char info_service[256];
  getnameinfo(res->ai_addr,
              int(res->ai_addrlen),
              info_host,
              sizeof(info_host),
              info_service,
              sizeof(info_service),
              0);
  device_name_ = info_host;

  /* Create Socket. */
  if ((sockfd_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == INVALID_SOCKET) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "Error on socket creation: %s",
              get_send_errors(WSAGetLastError()));
    /* LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    return;
  }

  /* Loose the "socket already in use" error. */
  if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "setsockopt error: %s",
              get_send_errors(WSAGetLastError()));
    /* LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, error_buffer); */
    return;
  }

  /* Bind. */
  if (bind(sockfd_, res->ai_addr, int(res->ai_addrlen)) == -1) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "Error on binding: %s",
              get_send_errors(WSAGetLastError()));
    /* LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    return;
  }

  freeaddrinfo(res); /* Free the data. */

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Wait for Connections
   * \{ */

  /* If name is "NONE", use the device name or in the case of the same device, use program ID. */
  if (name_ == "NONE") {
    name_ = device_name_;
  }
  /* IGNORE ALL ABOVE LETS CALL US 'HOST' */
  name_ = "HOST";
  /* This is done so that indentifying if we are the host is way easier, but maybe this will get
   * removed later. If a public network is used, the program will hole punch. */
  if (moves == dance_moves::PUBLIC) {
    if (listen_thread_.joinable()) {
      listen_thread_.join();
    }
    if (callback_thread_.joinable()) {
      callback_thread_.join();
    }

    /* Used as listen_thread, since this will transform into listening after its done. */
    listen_thread_ = std::thread([this] { this->hole_punch(); });
    return;
  }

  /* First check and possibly clean up old mess. */
  if (listen_thread_.joinable()) {
    listen_thread_.join();
  }
  if (callback_thread_.joinable()) {
    callback_thread_.join();
  }

  /* Now the program waits for incomming connections.
   * Create a thread that will listen for a return message, this will add connections to our map of
   * users. */
  listen_thread_ = std::thread([this] { this->listen(true); });
  /* Another one for the callbacks if the program is said to use them. */
  if (use_callback_functions_) {
    callback_thread_ = std::thread([this] { this->send_callbacks(); });
  }

  /** \} */
}

bool Dance::MU_connect(dance_moves moves, const char *host_IP, const char *port, bool _force_IPV4)
{
  /* -------------------------------------------------------------------- */
  /** \Set variables
   * \{ */

  char other_IP[39] = " ";
  char own_IP[39] = " ";

  force_IPV4_ = _force_IPV4;
  current_moves_ = moves;
  is_host_ = false;
  strcpy_s(other_IP, host_IP);

  /* Check if the inserted port is valid, otherwise just use the host_port. */
  int test_port = 0;
  if (port != NULL) {
    test_port = atoi(port);
  }
  if (test_port <= 1024 || test_port >= 49151) {
    port = host_port_;
    char error_buffer[256];
    sprintf_s(error_buffer, sizeof(error_buffer), "Default port: %s is used", host_port_);
    /*LOGLINE(DLogObj, DanceLogger::DANCE_INFO, error_buffer); */
  }
  else {
    memcpy_s(host_port_, sizeof(host_port_), port, strlen(port));
  }

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Set IPs
   * \{ */

  /* If we are on the same device, we just use 'localhost' as our IP. */
  if (moves == SAMEDEVICE) {
    /* Use #strcpy_s to set #other_IP to #localhost. */
    const char *same_device_input = "localhost";
    strcpy_s(other_IP, same_device_input);
    strcpy_s(own_IP, same_device_input);
    ipv_ = AF_INET; /* Set to IPV4. */
  }
  else {
    bool p = moves == PUBLIC ? true : false;
    strcpy_s(own_IP, MU_get_IP(p).c_str()); /* Set #own_IP using #MU_get_IP(). */
  }
  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Important variables
   * \{ */

  struct addrinfo hints, *res, *dest;
  int status;

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Add host IP to connections
   * \{ */

  /* Handle this and put it inside the map. */
  memset(&hints, 0, sizeof hints);
  hints.ai_family = ipv_;
  hints.ai_socktype = SOCK_DGRAM;
  /* #addrinfo */
  if ((status = getaddrinfo(other_IP, host_port_, &hints, &res)) != 0) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "getaddrinfo error on host: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    reset_state();
    return false;
  }

  /* Add host to map
   */
  them_addrss_.add("HOST", res);
  them_numbers_.add("HOST", 0);

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Get address information for this device
   * \{ */

  res = nullptr;
  memset(&hints, 0, sizeof(hints)); /* Reset #hints. */
  hints.ai_family = ipv_;           /* Use #IPV4 or #IPV6. */
  hints.ai_socktype = SOCK_DGRAM;   /* Use UPD. */
  hints.ai_protocol = IPPROTO_UDP;

  /* If we are binding using ipv4, we use our private IP. */
  char own_IP2[65];
  if (moves == PUBLIC && ipv_ == AF_INET) {
    strcpy_s(own_IP2, MU_get_IP(false).c_str());
  }
  else {
    strcpy_s(own_IP2, own_IP);
  }

  /* Get address info of ourself. */
  if ((status = getaddrinfo(own_IP2, host_port_, &hints, &res)) != 0) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "getaddrinfo error: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    reset_state();
    return false;
  }

  /* Get name of ourself. */
  char info_host[256];
  char info_service[256];
  getnameinfo(res->ai_addr,
              int(res->ai_addrlen),
              info_host,
              sizeof(info_host),
              info_service,
              sizeof(info_service),
              0);
  device_name_ = info_host;

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Create socket and possibly bind
   * \{ */

  /* Create Socket. */
  if ((sockfd_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == INVALID_SOCKET) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "Error on socket creation: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    reset_state();
    return false;
  }

  /* Bind only if public IP is used, the program does not need to bind elsewere. */
  if (moves == PUBLIC) {
    if (bind(sockfd_, res->ai_addr, int(res->ai_addrlen)) == -1) {
      char error_buffer[256];
      sprintf_s(error_buffer,
                sizeof(error_buffer),
                "Error on binding: %s",
                get_send_errors(WSAGetLastError()));
      /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
      reset_state();
      return false;
    }
  }

  freeaddrinfo(res); /* Free the data. (It is not used anymore). */

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Connect to host
   * \{ */

  /* If name is "NONE", use the device name or in the case of the same device, use program ID. */
  if (moves == SAMEDEVICE && name_ == "NONE") {
    name_ = std::to_string(GetCurrentProcessId());
  }
  else if (name_ == "NONE") {
    name_ = device_name_;
  }

  /* If the network is public, hole punching is needed. */
  if (moves == PUBLIC) {
    if (listen_thread_.joinable()) {
      listen_thread_.join();
    }
    if (callback_thread_.joinable()) {
      callback_thread_.join();
    }

    /* Used as listen_thread, since this will transform into listening after its done. */
    listen_thread_ = std::thread([this] { this->hole_punch(); });

    return true;
  }

  /* Send a message to the host to connect.
   * message Structure: 'Con' + 'name of the device' */
  std::string connect_message = "Con" + name_;

  /* Get address info of the host. */
  if ((status = getaddrinfo(other_IP, host_port_, &hints, &dest)) != 0) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "getaddrinfo error: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    reset_state();
    return false;
  }

  /* Connect for windows to send the error message to this socket, otherwise it may not does this.
   */
  if (connect(sockfd_, dest->ai_addr, int(dest->ai_addrlen)) != 0) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "connection error: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, error_buffer); */
    reset_state();
    return false;
  }

  /* Using #send (not #sendto), because the program is connected and has set up a default location.
   */
  if (send(sockfd_, connect_message.c_str(), int(connect_message.size()), 0) == -1) {
    char error_buffer[256];
    sprintf_s(
        error_buffer, sizeof(error_buffer), "Send failed: %s", get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, error_buffer); */
    reset_state();
    return false;
  }

  Sleep(50); /* Wait for package to arrive safely. */
  char buffer[MAXPACKAGESIZE];
  int result = 0;

  /* Unblock socket. */
  u_long iMode = 1;
  if (ioctlsocket(sockfd_, FIONBIO, &iMode) != 0) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "Could not unblock socket: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, error_buffer); */
    reset_state();
    return false;
  }

  if ((result = recv(sockfd_, buffer, MAXPACKAGESIZE, MSG_PEEK)) < 0)
  { /* This message can be assumed safely, since this is a conConfirm. */
    if (WSAGetLastError() == 10054) { /* Connection reset by peer. */
      /*LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, "Could not connect to socket, is the host
       * connected?"); */
      reset_state();
      return false;
    }
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "Receive error trying to connect, did the host lag out?: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    reset_state();
    return false;
  }

  /* Block socket again. */
  iMode = 0;
  if (ioctlsocket(sockfd_, FIONBIO, &iMode) != 0) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "Could not block socket: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, error_buffer); */
    reset_state();
    return false;
  }

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Create threads
   * \{ */

  /* First check and possibly clean up old mess. */
  if (listen_thread_.joinable()) {
    listen_thread_.join();
  }
  if (callback_thread_.joinable()) {
    callback_thread_.join();
  }

  /* Now create a thread that we will listen on indeffinitely. */
  listen_thread_ = std::thread([this] { this->listen(true); });

  /* Another one for the call backs, if hte program is set to use them. */
  if (use_callback_functions_) {
    callback_thread_ = std::thread([this] { this->send_callbacks(); });
  }

  return true;

  /** \} */
}

void Dance::hole_punch()
{
  hole_punching_status_ = 1;

  /* Generate seed. */
  const uint32_t seed = int(std::chrono::system_clock::now().time_since_epoch().count());
  /* Use this seed to get random numbers. */
  srand(seed);

  /* Create vector of all connections that has to be made. */
  blender::Vector<struct addrinfo *> future_connections;

  for (const auto &value : them_addrss_.values()) {
    future_connections.append(value);
  }

  /* While-loop until everyone is connected. */
  while (future_connections.size() > 0) {
    /* Thread to listen. */
    connection_made_ = std::async(std::launch::async, [this] { return this->listen(false); });

    while (true) {
      auto status = connection_made_.wait_for(std::chrono::milliseconds(100));
      if (status == std::future_status::ready) {
        if (connection_made_.get()) {
          break; /* Connection was successful. */
        }
        /* Note that it is possible for status to be true, but this user had already a connection
         * with this device, so the program did not progress. */
      }

      /* Sending message. */
      const std::string hole_punch_message = "Con " + name_ + ": HolePunch";

      /* If there was a succefull connection on the first iteration, it will finish the other 9
       * before realizing. */
      for (int j = 0; j < 10; j++) {
        /* Iterate over all the other peers */
        for (int i = 0; i < int(future_connections.size()); i++) {
          send_message(hole_punch_message.c_str(),
                       int(hole_punch_message.size()),
                       future_connections[i],
                       false,
                       nullptr);
        }
        /* Sleep for between 0.09 and 0.11 seconds. */
        Sleep(rand() % 2 + 9);
      }
    }

    /* Check which one connected and remove it from the vector. */
    for (int i = 0; i < int(hole_punch_confirmed_connections_.size()); i++) {
      for (int j = 0; j < int(future_connections.size()); j++) {
        if (hole_punch_confirmed_connections_[i]->ai_addrlen ==
                future_connections[j]->ai_addrlen &&
            memcmp(hole_punch_confirmed_connections_[i]->ai_addr,
                   future_connections[j]->ai_addr,
                   hole_punch_confirmed_connections_[i]->ai_addrlen) == 0)
        {
          /* remove from futureconnections. */
          future_connections.remove(j);
        }
      }
    }
  }
  /* Clear vector, it is not needed anymore. */
  hole_punch_confirmed_connections_.clear();
  hole_punching_status_ = 2;

  /* Create a thread for callback. */
  if (use_callback_functions_) {
    callback_thread_ = std::thread([this] { this->send_callbacks(); });
  }

  /* Now start listening until stopped. */
  listen(true);
}

void Dance::MU_keep_alive(float dt)
{
  if (current_moves_ == PUBLIC && is_host_) {
    keep_alive_time_ -= dt;
    if (keep_alive_time_ <= 0.0f) {
      keep_alive_time_ = 60.0f;

      const char *message = "KeepAliveMessage";
      /* Send message all other connections. */
      for (const auto &value : them_addrss_.values()) {
        send_message(message, int(strlen(message)), value, false, nullptr);
      }
    }
  }
  else /* Send to every connection a #KeepAliveMessage message. */ {
    quick_keep_alive_time_ -= dt;
    if (quick_keep_alive_time_ <= 0.0f) {
      quick_keep_alive_time_ = 0.5f;

      const char *message = "KeepAliveMessage";
      /* Send message to every connection. */
      for (const auto &value : them_addrss_.values()) {
        send_message(message, int(strlen(message)), value, false, nullptr);
      }
    }
  }
}

void Dance::MU_get_package(std::string package_name,
                           bool delete_message,
                           return_package_info *r_package_info)
{
  message_vector_mutex_.lock();

  bool is_list = false;
  for (int i = int(received_messages_.size()) - 1; i >= 0; --i) {
    bool got_name = false;
    std::string tmp;
    int word_index = 0;
    r_package_info->succeeded = true;

    while (!(tmp = get_word(received_messages_[i], word_index++)).empty()) {
      /* First index is the name. */
      if (!got_name) {
        got_name = true;
        continue; /* Could use later. */
      }

      /* Now the word is checked to see if this is the correct package. */
      if (!is_list) {
        if (tmp != package_name) {
          break;
        }
        else {
          is_list = true;
          continue; /* Go to next word. */
        }
      }

      if (tmp == "end") {
        break;
      }

      bool parsed = false;

      std::apply(/* #apply unpacks the tuple arguments into the lamda so types will become the
                    types in the tuple. */
                 [&](auto... types) /* Lambda takes a reference pack of all the tuple types. */
                 {
                   (... ||
                    (parsed = MU_try_parse(
                         tmp,
                         types), /* #MU_try_parse tries to parse each empty type, #parsed will be
                                    true or false, types will be filled with input if succeeded. */
                     parsed ? (r_package_info->variable_vector.append(types), true) :
                              false)); /* If parsed, push back the type and return true, else
                                          return false. */
                 },
                 std::tuple<int, float, double, uint32_t, bool, std::string>{}); /* Types that will
                                                                                    be tested. */
      if (!parsed) {
        /* Just insert the string. */
        r_package_info->variable_vector.append(tmp);
        /* Let know that not all variables succeeded to parse */
        r_package_info->succeeded = false;
      }
    }
    /* Gathered all variables (hopefully) */
    if (delete_message && r_package_info->succeeded && is_list) {
      received_messages_.erase(received_messages_.begin() + i); /* Possibly delete this message. */
    }
    if (is_list) {
      break;
    }
  }
  if (!is_list) {
    r_package_info->succeeded = false;
  }
  message_vector_mutex_.unlock();
}

/* Listen for incomming messages. */

bool Dance::listen(bool keep_checking)
{
  const int max_size_message_vector_num = 100;
  struct sockaddr_storage them_addr{}; /* Adress of the others. */

  /* Up the writing size and reading size. */
  int send_buffer_size = 16 * 1024;    /* 16 KB send buffer size. */
  int receive_buffer_size = 32 * 1024; /* 32 KB receive buffer size */
  if (setsockopt(
          sockfd_, SOL_SOCKET, SO_SNDBUF, (char *)&send_buffer_size, sizeof(send_buffer_size)) < 0)
  {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "Error setting send buffer size: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, error_buffer); */
  }
  /* Set receive buffer size. */
  if (setsockopt(sockfd_,
                 SOL_SOCKET,
                 SO_RCVBUF,
                 (char *)&receive_buffer_size,
                 sizeof(receive_buffer_size)) < 0)
  {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "Error setting receive buffer size: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, error_buffer); */
  }

  /* Set current time. */
  last_time_ = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());

  quit_listening_ = false;
  while (!quit_listening_) {

    socklen_t addr_size = sizeof(sockaddr_storage);
    const int buffer_len = recvfrom(sockfd_,
                                    static_cast<char *>(listen_buffer),
                                    sizeof(listen_buffer),
                                    0,
                                    reinterpret_cast<struct sockaddr *>(&them_addr),
                                    &addr_size);

    if (buffer_len < 0) {
      /* Highly likely that the connection was reset by peer, if not, check it out! */
      // const bool removed_peer = handle_disconnection(them_addr, addr_size);

      // if (removed_peer && !is_host_) { /* TODO: what if Public connection? */
      //   quit_listening_ = true;
      //   listen_thread_.detach();
      //   return false;
      // }
      if (true) {
        char error_buffer[256];
        sprintf_s(error_buffer,
                  sizeof(error_buffer),
                  "Error receiving message: %s",
                  get_send_errors(WSAGetLastError()));
        printf(get_send_errors(WSAGetLastError()));
        /*LOGLINE(DLogObj, DanceLogger::DANCE_INFO, error_buffer); */
      }
    }
    else if (buffer_len == 0) {
      char error_buffer[256];
      sprintf_s(error_buffer,
                sizeof(error_buffer),
                "Buffer of 0 received, connection closed?: %s",
                get_send_errors(WSAGetLastError()));
      /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    }
    else {
      /* Check buffer size DEBUG.
       * u_long bytesAvailable;
       * if (ioctlsocket(sockfd_, FIONREAD, &bytesAvailable) == 0)
       *{
       *     std::cout << "Bytes available in the receive buffer: " << bytesAvailable << std::endl;
       * }
       * else
       *{
       *     perror("Error querying receive buffer size\n");
       * } */

      if (buffer_len >= MAXPACKAGESIZE) {
        char error_buffer[256];
        sprintf_s(error_buffer,
                  sizeof(error_buffer),
                  "Message too long: %s",
                  get_send_errors(WSAGetLastError()));
        /*LOGLINE(DLogObj, DanceLogger::DANCE_INFO, error_buffer); */
        continue;
      }

      /* Received message. */
      printf("Message received: %s\n", listen_buffer);
      bool connecting_message = false;
      std::string other_name{};
      listen_buffer[buffer_len] = '\0'; /* Null terminate the received message. */

      /* -------------------------------------------------------------------- */
      /** \Important Message?
       * \{ */

      /* Check if it is an important message. */
      if (std::strncmp(listen_buffer, "Imp", 3) == 0) {

        /* Remove 'Imp ' from the buffer. */
        memcpy_s(
            listen_buffer, sizeof(listen_buffer), &listen_buffer[3], sizeof(listen_buffer) - 3);
        const std::string buf(listen_buffer);
        const std::string ID = std::string(get_word(buf, 0));
        /* Remove ID from buffer. */
        memcpy_s(listen_buffer,
                 sizeof(listen_buffer),
                 &listen_buffer[ID.length() + 1],
                 sizeof(listen_buffer) - (ID.length() + 1));

        bool long_full = true;
        /* Check if it is a long message */
        if (std::strncmp(listen_buffer, "Long", 4) == 0) {
          /* Remove 'Long' from the buffer. */
          memcpy_s(
              listen_buffer, sizeof(listen_buffer), &listen_buffer[4], sizeof(listen_buffer) - 4);
          /* Find numbers */
          std::string num_1;
          int num_2, idx_last;
          std::string number_finder(listen_buffer);
          number_finder = number_finder.substr(0, number_finder.find('/'));
          num_1 = number_finder;
          number_finder = listen_buffer;
          number_finder = number_finder.substr(number_finder.find('/') + 1,
                                               idx_last = number_finder.find(' '));
          num_2 = std::stoi(number_finder);
          idx_last++;
          /* Remove numbers from the buffer. */
          memcpy_s(listen_buffer,
                   sizeof(listen_buffer),
                   &listen_buffer[idx_last],
                   sizeof(listen_buffer) - idx_last);

          /* Add value to map */
          blender::Map<std::string, std::string> batch_map;
          auto &batch_map_ret = long_message_storage_.lookup_or_add(ID, batch_map);
          std::string *value_ptr = batch_map_ret.lookup_ptr(num_1);
          if (value_ptr) {
            batch_map_ret.add(num_1, listen_buffer);
          }

          /* Check if we got all. */
          if (batch_map_ret.size() < num_2) {
            long_full = false;
          }
          else {
            std::string fullMessage;
            fullMessage.reserve(num_2 * 1024);
            for (int i = 0; i < batch_map_ret.size(); i++) {
              std::string *batch_message = batch_map_ret.lookup_ptr(std::to_string(i));
              if (batch_message) {
                fullMessage += *batch_message;
              }
              else {
                /* Something went wrong, full message is not complete. */
                long_full = false;
                // const char *error_buffer =
                //     "Not all batch messages were complete trying to recreate full message";
                /*LOGLINE(DLogObj, DanceLogger::DANCE_MESSAGE, error_buffer); */
                break;
              }
            }
            if (long_full) {
              long_message_storage_.remove(ID);
            }
          }
        }

        /* Send back that it succeeded. */
        addrinfo *res = storage_to_addr_info(them_addr, addr_size);
        const std::string return_message = "SImp" + ID; /* Succes Important. */
        send_message(return_message.c_str(), int(return_message.size()), res, false, nullptr);

        /* We do not want to handle batch messages seperately */
        if (!long_full) {
          continue;
        }
      }

      /* Check if it is a succes important message. */
      if (std::strncmp(listen_buffer, "SImp", 4) == 0) {
        /* Remove 'SImp' from the buffer. */
        memcpy_s(
            listen_buffer, sizeof(listen_buffer), &listen_buffer[4], sizeof(listen_buffer) - 4);
        /* Remove from array. */
        important_send_messages_.remove(atoi(listen_buffer));
        continue;
      }

      /* Check on the other important messages in array. */
      if (!important_send_messages_.is_empty()) {
        const uint64_t ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        const float dt = static_cast<float>(ms - last_time_) * 0.001f;
        last_time_ = ms;
        time_checked_imp_ += dt;

        /* Did enough time passed? */
        if (time_checked_imp_ > 0.1f) {
          time_checked_imp_ = 0.0f;

          for (auto &value : important_send_messages_.values()) {
            if (value.checks_done >= 5) {

              /* Probably do something */
              if (value.message.size() < 200) {
                char error_buffer[256];
                sprintf_s(error_buffer,
                          sizeof(error_buffer),
                          "Never received important message confirmation from message: %s",
                          value.message.c_str());
                /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
              }
              else {
                /*LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, "Needed to send important message a
                 * second time"); */
                printf("Never received important message confirmation from message");
              }
            }
            else /* Send again. */ {

              /* Get user_ID. */
              const int userID = static_cast<int>(
                  static_cast<float>(value.ID) /
                  std::powf(10.0f, std::floorf(std::log10(static_cast<float>(value.ID)))));

              /* TODO: is user_ID correct? */
              if (value.message.size() < 200) {
                char error_buffer[256];
                sprintf_s(error_buffer,
                          sizeof(error_buffer),
                          "Needed to send this important message a second time: %s",
                          value.message.c_str());
                /*LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, error_buffer); */
              }
              else {
                /*LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, "Needed to send important message a
                 * second time"); */
              }

              const std::string imp_return_message = "Imp" + std::to_string(value.ID) + " " +
                                                     value.message; /* SImp = Succes Important. */

              for (const auto &item : them_addrss_.items()) {
                if (them_numbers_.lookup(item.key) == userID) { /* Send to this user. */
                  send_message(imp_return_message.c_str(),
                               int(imp_return_message.size()),
                               item.value, false,
                               nullptr);
                }
              }

              value.checks_done++;
            }
          }
        }
      }

      /** \} */

      /* -------------------------------------------------------------------- */
      /** \Connection message?
       * \{ */

      /* Check if this is a connection message. */
      if (std::strncmp(listen_buffer, "Con", 3) == 0) {
        /* If it is a #ConConfirm, and we don't want to keep checking, we can return. */
        if (std::strncmp(listen_buffer, "ConConfirm", 10) == 0) {
          /* Set our number */
          sscanf_s(&listen_buffer[10], "%d", &peer_ID);
          if (keep_checking) {
            continue;
          }
          else {
            return true;
          }
        }
        /* Check if it is above our max connections. */
        if (them_addrss_.size() >= max_connections_) {
          continue;
        }
        connecting_message = true;
        other_name = &listen_buffer[3]; /* Name starts after the 'Con'. */
      }

      /* Check if it is a disconnect message. */
      else if (std::strncmp(listen_buffer, "Dis", 3) == 0) {
        if (handle_disconnection(them_addr, addr_size)) {
          if (!is_host_) {
            quit_listening_ = true;
            return false;
          }
          else {
            continue;
          }
        }
      }

      /** \} */

      /* -------------------------------------------------------------------- */
      /** \Amount of connections -message?
       * \{ */

      /* Check if this is a message to check how many connections we have (non-host only). */
      else if (!is_host_ && std::strncmp(listen_buffer, "Tot", 3) == 0) {
        /* Check for certainty if it really is this message. */
        if (std::strncmp(listen_buffer, "TotalConCount", 13) == 0) {
          /* Update total connection count. */
          const std::string buf(listen_buffer);
          total_connections_ = std::stoi(get_word(buf, 1).data());
          continue;
        }
      }

      /** \} */

      /* -------------------------------------------------------------------- */
      /** \Package-style message
       * \{ */

      /* Check if it is a sendTo message, if so, this program is the host */
      else if (std::strncmp(listen_buffer, "-To", 3) == 0) {
        /* Send to another client */
        if (std::strncmp(listen_buffer, "-ToCl-", 6) == 0) {
          /* Support for 99 users */
          int extra = 0;
          std::string ID = std::to_string(listen_buffer[6]);
          if (listen_buffer[7] != ' ') {
            ID += std::to_string(listen_buffer[7]);
            extra++;
          }
          const std::string message = &listen_buffer[8 + extra];
          MU_send_message_to(message, std::stoi(ID), false, false, false);
          continue; /* We dont want to do anything with this message so move on. */
        }
        /* Handle it like a normal message but remove the first part. */
        else if (is_host_ && std::strncmp(listen_buffer, "-ToHo", 5) == 0) {
          /* Fill in the name. */
          memcpy_s(
              listen_buffer, sizeof(listen_buffer), &listen_buffer[7], sizeof(listen_buffer) - 7);
          int i = 0;
          while (i < strlen(listen_buffer) && listen_buffer[i] != ':') {
            other_name += listen_buffer[i++];
          }
        }
        /* Send to all, including this program but not to the sender. */
        else if (std::strncmp(listen_buffer, "-ToAll", 6) == 0) {
          const std::string message = &listen_buffer[7];
          const addrinfo *res = storage_to_addr_info(them_addr, addr_size);
          for (const auto &item : them_numbers_.items()) {
            if (item.value != 0 || res != them_addrss_.lookup(item.key)) {
              /* Don't send to host (ourself) or the sender.*/
              MU_send_message_to(message, item.value, false, false, false);
            }
          }
          /* Now handle it ourself. */

          /*  Fill in the name. */
          memcpy_s(
              listen_buffer, sizeof(listen_buffer), &listen_buffer[7], sizeof(listen_buffer) - 7);
          int i = 0;
          while (i < strlen(listen_buffer) && listen_buffer[i] != ':') {
            other_name += listen_buffer[i++];
          }
        }
      }

      if (other_name.empty()) {
        int i = 0;
        while (i < strlen(listen_buffer) && listen_buffer[i] != ':') {
          other_name += listen_buffer[i++];
        }
      }

      /* If connection type is #SAMEDEVICE, the name is checked to prevent sending messages to the
       * sender. */
      if (current_moves_ == SAMEDEVICE && name_ == other_name) {
        /* This program is sending the messsages, so ignore */
        /* printf("Ignored above message\n"); */
        continue;
      }

      /* Store package to queue. */
      if (use_callback_functions_) {
        callback_mutex_.lock();
        user_package_storage_.push(listen_buffer);
        callback_mutex_.unlock();
      }

      /* Store message. */
      message_vector_mutex_.lock();
      received_messages_.push_front(listen_buffer);
      /* Delete oldest message if vector is too big. */
      if (received_messages_.size() > max_size_message_vector_num) {
        char error_buffer[256];
        if (received_messages_.back().size() > 200) {
          sprintf_s(error_buffer,
                    sizeof(error_buffer),
                    "Message getting deleted due to buffer being full: %s",
                    "Could not print message (message was too long)");
        }
        else {
          sprintf_s(error_buffer,
                    sizeof(error_buffer),
                    "Message getting deleted due to buffer being full: %s",
                    received_messages_.back().c_str());
        }
        /*LOGLINE(DLogObj, DanceLogger::DANCE_INFO, error_buffer); */
        received_messages_.pop_back();
      }
      message_vector_mutex_.unlock();

      /* Only do this next part if it is a connection message */
      if (!connecting_message) {
        continue;
      }

      /** \} */

      /* -------------------------------------------------------------------- */
      /** \Do we have the user already?
       * \{ */

      /* Convert from storage to addrinfo. */
      addrinfo *res = storage_to_addr_info(them_addr, addr_size);

      /* Now check if we already had this client */
      bool got_them = false;
      for (const auto &item : them_addrss_.items()) {
        /* Use 2 ways to check, one with the name and otherwise use the #sa_data. */
        if (item.key == other_name) {
          got_them = true;
        }
        else if (res->ai_addrlen == item.value->ai_addrlen &&
                 memcmp(res->ai_addr, item.value->ai_addr, res->ai_addrlen) == 0)
        {
          got_them = true;
        }
      }
      if (!got_them) {
        printf("Welcome to the network: %s!\n", other_name.c_str());

        /* Add to map. */
        them_addrss_.add(other_name, res);
        them_numbers_.add(other_name, at_player_number_);
        at_player_number_++;
        /* Send message of our new total connections. */
        if (is_host_) {
          total_connections_ = int(them_addrss_.size());
          const char *total_amount_message = "TotalConCount ";
          char connection_amount_message[50];
          const int message_size = snprintf(
              connection_amount_message, 50, "%s%i", total_amount_message, total_connections_);

          /* Iterate over all the other peers. */
          for (const auto &value : them_addrss_.values()) {
            send_message(connection_amount_message, message_size, value, false, nullptr);
          }
        }

        /* Send a confirm that the message reached us succesfully, also give them their number */
        std::string confirm_message = "ConConfirm" + std::to_string(at_player_number_ - 1);
        send_message(confirm_message.c_str(), int(confirm_message.size()), res,false, nullptr);
        /* Now return if we dont want to keep checking. */
        if (!keep_checking) {
          return true;
        }
      }

      /* Hole Punching Check. */
      /* #listen() being called from #holepunch(). */
      if (hole_punching_status_ == 1) {
        /* Is client already in the hole punching vector? */
        got_them = false;
        for (int i = 0; i < int(hole_punch_confirmed_connections_.size()); i++) {
          if (res->ai_addrlen == hole_punch_confirmed_connections_[i]->ai_addrlen &&
              memcmp(res->ai_addr,
                     hole_punch_confirmed_connections_[i]->ai_addr,
                     res->ai_addrlen) == 0)
          {
            got_them = true;
          }
        }
        if (!got_them) {
          total_connections_++;
          hole_punch_confirmed_connections_.append(res);
        }
      }
    }
  }
  return false;
}

/* Using help from chatGPT. */
void Dance::send_callbacks()
{
  quit_callback_ = false;
  while (!quit_callback_) {
    while (!user_package_storage_.empty()) {
      const std::string package_name = std::string(get_word(user_package_storage_.front(), 1));
      /* Find function belonging to this package name, if it found one, execute it. */
      if (package_name.c_str()) {
        const auto function = callback_functions_.lookup_try(package_name.c_str());
        if (function) {
          (*function)(user_package_storage_.front());
        }
        else {
          printf("function not found\n");
        }
      }
      user_package_storage_.pop();
    }
  }
}

void Dance::reset_state()
{
  ipv_ = AF_INET;
  total_connections_ = 0;
  sockfd_ = 0;
  is_host_ = false;

  if (callback_thread_.joinable()) {
    quit_callback_ = true;
    callback_thread_.join();
  }
  hole_punching_status_ = 0;
  current_moves_ = SAMEDEVICE;

  max_connections_ = 10;
  received_messages_.clear();
  std::queue<std::string>().swap(user_package_storage_);
  callback_functions_.clear();
  use_callback_functions_ = false;
  force_IPV4_ = false;

  them_addrss_.clear();
  them_numbers_.clear();
  hole_punch_confirmed_connections_.clear();
}

void Dance::send_message(const char *message,
                         int message_size,
                         addrinfo *adress,
                         bool important,
                         const char *send_error)
{
  /* Check if message is too long to send, max is 1024, yet we check for 964 to be able to cut off
   * a word correctly and add other info */
  if (message_size > 964) {
    int amount_messages = int(std::ceil(float(message_size) / 964));
    int start_index = 0;
    int at_index = 964;

    /* Find key */
    int peer_number = -1;
    for (const auto key : them_addrss_.keys()) {
      if (memcmp(adress->ai_addr, them_addrss_.lookup(key)->ai_addr, adress->ai_addrlen) == 0) {
        peer_number = them_numbers_.lookup(key);
        break;
      }
    }

    for (int i = 0; i < amount_messages; i++) {

      char part_message[1024];
      snprintf(part_message, at_index - start_index, "%s", &message[start_index]);

      /* Add format info */
      char format_message[64];
      sprintf_s(format_message, "Long%d/%d ", i, amount_messages - 1);
      std::string seperate_message = format_message + std::string(part_message);


      /* Make important */
      add_important_message(seperate_message, peer_number);

      /* Send message. */
      if (sendto(sockfd_,
                 seperate_message.c_str(),
                 int(seperate_message.size()),
                 0,
                 adress->ai_addr,
                 adress->ai_addrlen) ==
          -1)
      {
        if (send_error == nullptr || strlen(send_error) < 200) {
          char error_buffer[256];
          sprintf_s(error_buffer,
                    sizeof(error_buffer),
                    "Send failed: %s",
                    get_send_errors(WSAGetLastError()));
        }
        else {
          /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, send_error); */
        }
      }

      start_index = at_index;
      at_index += 964;
    }
  }
  /* If message is not too long, send it. */
  else {
    std::string message_string;
    if (important) {
      int peer_number = -1;
      for (const auto key : them_addrss_.keys()) {
        if (memcmp(adress->ai_addr, them_addrss_.lookup(key)->ai_addr, adress->ai_addrlen) == 0) {
          peer_number = them_numbers_.lookup(key);
        }
      }
      add_important_message(message_string, peer_number);
    }
    else {
      message_string = message;
    }

    if (sendto(sockfd_,
               message_string.c_str(),
               int(message_string.size()),
               0,
               adress->ai_addr,
               adress->ai_addrlen) == -1)
    {
      if (send_error == nullptr || strlen(send_error) < 200) {
        char error_buffer[256];
        sprintf_s(error_buffer,
                  sizeof(error_buffer),
                  "Send failed: %s",
                  get_send_errors(WSAGetLastError()));
      }
      else {
        /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, send_error); */
      }
    }
  }
}

Dance::important_message_struct Dance::add_important_message(std::string &message,
                                                             const int to_user_ID,
                                                             uint32_t SetID)
{
  important_message_struct important_message;
  if (SetID == 0) {
    /* Create ID based on count and user ID in the front. */
    float mult = std::pow(10.0f,
                          std::floorf(std::log10(static_cast<float>(at_imp_message_ * 10))));
    important_message.ID = static_cast<uint32_t>(to_user_ID * mult) + at_imp_message_++;
  }
  else {
    important_message.ID = SetID;
  }
  important_message.message = message;
  important_message.checks_done = 0;
  important_send_messages_.add(important_message.ID, important_message);
  message = "Imp" + std::to_string(important_message.ID) + " " + message;
  return important_message;
}

bool Dance::handle_disconnection(sockaddr_storage input, int input_size)
{
  /* If not the host, a peer has closed the connection, so this peer will be removed. */
  bool disconnected = false;
  /* Check if the user is in the map. */
  const addrinfo *res = storage_to_addr_info(input, input_size);

  for (const auto &item : them_addrss_.items()) {
    if (memcmp(res->ai_addr, item.value->ai_addr, res->ai_addrlen) == 0) {
      /* remove from vector. */
      printf("Peer %s has disconnected...\n", item.key.c_str());
      disconnected_user_IDs_.append(them_numbers_.lookup(item.key));
      them_numbers_.remove(item.key);
      them_addrss_.remove(item.key);
      disconnected = true;

      /* Update total amount of connections to all peers if host. */
      if (is_host_) {
        total_connections_ = int(them_addrss_.size());
        const char *total_amount_message = "TotalConCount ";
        char amount_message[50];
        const int message_size = snprintf(
            amount_message, 50, "%s%i", total_amount_message, total_connections_);

        /* Iterate over all the other peers. */
        for (const auto &value : them_addrss_.values()) {
          send_message(amount_message, message_size, value, false, nullptr);
        }
      }
      else {
        /* Reset state, since the host was our everything. */
        reset_state();
      }
      return disconnected;
    }
  }
  return disconnected;
}

std::string_view Dance::get_word(const std::string &input, int word_number)
{
  if (&input == nullptr || input.empty()) {
    return {};
  }

  int end_of_word = 0;
  int begin_of_word = 0;

  /* Get to the word. */
  int j = 0;
  for (int i = 0; i < word_number; i++) {
    while (input[j] != 32 && input[j] != '\0') {
      j++;
    }
    /* Find next non-space. */
    while (input[j] == 32 && input[j] != '\0') {
      j++;
    }
  }
  /* Get the word. */
  begin_of_word = j;
  while (j < int(input.size()) && input[j] != 32 && input[j] != '\0') {
    j++;
  }
  end_of_word = j;

  if (begin_of_word != end_of_word) {
    return std::string_view(input.data() + begin_of_word, end_of_word - begin_of_word);
  }

  return {};
}

/* -------------------------------------------------------------------- */
/** \Helper functions
 * \{ */

std::string Dance::MU_get_IP(bool public_IP)
{
  if (public_IP) {
    const std::string website_HTLM =
        get_website(); /* returns #IPV4 or #IPV6, whichever is possible */
    if (website_HTLM != "0") {
      char OutputIP[65];
      strcpy_s(OutputIP, website_HTLM.c_str()); /* Copy string to IP char. */
      return OutputIP;
      /* printf("Your IP adress = %s\n", website_HTLM.c_str()); */
    }
  }
  else {
    /* Use the code from microsoft.
     * https://learn.microsoft.com/nl-nl/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersinfo?redirectedfrom=MSDN
     */
    PIP_ADAPTER_INFO pip_adapter_info;
    PIP_ADAPTER_INFO pip_adapter = NULL;
    DWORD return_value = 0;

    ULONG output_buffer_length = sizeof(IP_ADAPTER_INFO);
    pip_adapter_info = (IP_ADAPTER_INFO *)malloc(sizeof(IP_ADAPTER_INFO));
    if (pip_adapter_info == NULL) {
      /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, "Error allocating memory needed to call
       * GetAdaptersinfo1"); */
    }
    /* Make an initial call to #GetAdaptersInfo to get the necessary size into the
     * #output_buffer_length variable. */
    if (GetAdaptersInfo(pip_adapter_info, &output_buffer_length) == ERROR_BUFFER_OVERFLOW) {
      free(pip_adapter_info);
      pip_adapter_info = (IP_ADAPTER_INFO *)malloc(output_buffer_length);
      if (pip_adapter_info == NULL) {
        /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, "Error allocating memory needed to call
         * GetAdaptersinfo2"); */
      }
    }
    if ((return_value = GetAdaptersInfo(pip_adapter_info, &output_buffer_length)) == NO_ERROR) {
      pip_adapter = pip_adapter_info;
      while (pip_adapter) {
        const std::string IP = pip_adapter->IpAddressList.IpAddress.String;

        if (IP == "0.0.0.0") {
          pip_adapter = pip_adapter->Next;
          continue;
        }

        /* Check for 192.168.x.x, 10.x.x.x, and 172.16.x.x - 172.31.x.x */
        /* uint32_t a, b;
         * sscanf_s(IP.c_str(), "%u.%u", &a, &b);  // CHATGPT suggested this
         * if ((a == 192 && b == 168) || (a == 10) || (a == 172 && (b >= 16 && b <= 31)))
         *{
         *}
         * else
         *{
         *    // COULD BREAK IF ONLY A PUBLIC IP EXIST.
         *    pip_adapter = pip_adapter->Next;
         *    continue;  // Not a private IP.
         *} */

        /* Check for virtual machine by checking if device have a gateway */
        if (std::string(pip_adapter->GatewayList.IpAddress.String) == "0.0.0.0") {
          pip_adapter = pip_adapter->Next;
          continue; /* Virtual Machine. */
        }

        /* If gotten to this point, this is probably the correct IP. */
        char output_IP[65];
        strcpy_s(output_IP, IP.c_str()); /* Copy string to IP char. */
        return output_IP;

        /* For possibly contineuation: */
        /* Print IP Adress
         * printf("\tIP Address: \t%s\n", IP.c_str());
         * pip_adapter = pip_adapter->Next;
         * printf("\n"); */
      }
    }
    else {
      char error_buffer[256];
      sprintf_s(error_buffer,
                sizeof(error_buffer),
                "GetAdaptersInfo failed with error: %d\n",
                return_value);
      /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    }
    if (pip_adapter_info) {
      free(pip_adapter_info);
    }
  }
  return "0";
}

std::string Dance::get_website()
{
  /* Credits from https://stackoverflow.com/a/39567361 */
  WSADATA wsa_data;
  SOCKET socket_temp;
  SOCKADDR_IN6 *socket_address_6;
  SOCKADDR_IN *socket_address;
  std::string website_HTLM;
  char buffer[10000];
  std::string url = "api64.ipify.org";
  const std::string get_http = "GET / HTTP/1.1\r\nHost: " + url + "\r\nConnection: close\r\n\r\n";

  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, "WSASTARTUP failed trying to get the website"); */
    return "0";
  }

  struct addrinfo hints, *res, *p;
  int status;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  ipv_ = AF_INET6;

  /* Get address info of the site, note that port 80 is used: http */
  /* Comment to force #IPV4 */
  if (force_IPV4_ || (status = getaddrinfo(url.c_str(), "80", &hints, &res)) != 0) {
    /* Try it with #IPV4 */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    ipv_ = AF_INET;
    url = "api.ipify.org";
    if ((status = getaddrinfo(url.c_str(), "80", &hints, &res)) != 0) {
      char error_buffer[256];
      sprintf_s(error_buffer,
                sizeof(error_buffer),
                "error while getaddrinfo ourself: %s",
                get_send_errors(WSAGetLastError()));
      /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
      return "0";
    }
  }

  for (p = res; p != NULL; p = p->ai_next) {
    /* #IPV6 */
    if (hints.ai_family == AF_INET6) {
      socket_address_6 = (SOCKADDR_IN6 *)p->ai_addr;
    }
    /* #IPV4 */
    else if (hints.ai_family == AF_INET) {
      socket_address = (SOCKADDR_IN *)p->ai_addr;
    }
  }

  /* Get socket. */
  socket_temp = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

  /* And connect. */
  if (connect(socket_temp, res->ai_addr, int(res->ai_addrlen)) != 0) {
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "Could not connect: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    return "0";
  }

  if (send(socket_temp, get_http.c_str(), int(strlen(get_http.c_str())), 0) == -1)
  { /* Send request to site. */
    char error_buffer[256];
    sprintf_s(error_buffer,
              sizeof(error_buffer),
              "Could not send: %s",
              get_send_errors(WSAGetLastError()));
    /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
  }

  /* Sending a message to the site caused the site to send a message back with information.
   * We unpack this information (very badly) */
  int data_len;
  while ((data_len = recv(socket_temp, buffer, 10000, 0)) > 0) {
    int i = 0;
    while (buffer[i] >= 32 || buffer[i] == '\n' || buffer[i] == '\r') {
      /* Checks for 2 new lines and a number after. Here is the IP */
      if ((buffer[i] == '\n' || buffer[i] == '\r') &&
          (buffer[i + 1] == '\n' || buffer[i + 1] == '\r') && buffer[i + 2] >= 48 &&
          buffer[i + 2] <= 57)
      {
        i += 2;
        while (buffer[i] != '\n' && buffer[i] != '\r' && buffer[i] > 32) {
          website_HTLM += buffer[i];
          i++;
        }
        break;
      }

      i += 1;
    }
  }

  freeaddrinfo(res);
  closesocket(socket_temp);
  WSACleanup();

  return website_HTLM;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \Messages and packages
 * \{ */

void Dance::MU_send_message_to(
    std::string message, int them_ID, bool to_host, bool to_all, bool important)
{
  if (is_host_) {
    /* Iterate of numbers. */
    for (const auto &item : them_addrss_.items()) {
      const int cur_num = them_numbers_.lookup(item.key);
      if (to_all || cur_num == them_ID) /* Send to this one. */ {

        /* Send the message. */
        send_message(message.c_str(), int(message.size()), item.value, important, nullptr);
        /* Go back. */
        if (!to_all)
          return;
      }
    }
    /* If here, #them_ID is not available. */
    /*LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, "them ID is not found"); */
    return;
  }
  else {           /* not host. */
    if (to_host) { /* Ignore #them_ID, we send this one just to the host. */
      const std::string data_message = "-ToHo " + message;
      ;

      /* Send the message to host. */
      send_message(data_message.c_str(),
                   int(data_message.size()),
                   them_addrss_.lookup("HOST"),
                   important,
                   nullptr);
    }
    else if (to_all) {
      const std::string data_message = "-ToAll " + message;
      /* Send the message to host. */
      send_message(data_message.c_str(),
                   int(data_message.size()),
                   them_addrss_.lookup("HOST"),
                   important,
                   nullptr);
    }
    else { /* Send to the host, which will send it to the right user. */
      const std::string data_message = "-ToCl-" + std::to_string(them_ID) + " " + message;

      /* Send the message to host. */
      send_message(data_message.c_str(),
                   int(data_message.size()),
                   them_addrss_.lookup("HOST"),
                   important,
                   nullptr);
    }
  }
}

bool Dance::MU_is_disconnected(const int user_ID, bool remove)
{
  for (int i = 0; i < disconnected_user_IDs_.size(); i++) {
    if (user_ID == disconnected_user_IDs_[i]) {
      if (remove) {
        disconnected_user_IDs_.remove(i);
      }
      return true;
    }
  }
  return false;
}

void Dance::MU_send_package(std::string package_name, bool important)
{
  if (is_host_) {
    char data_message[MAXPACKAGESIZE];

    MU_prepare_package(package_name.c_str(), data_message);

    /* Iterate over all the other peers. */
    for (const auto &item : them_addrss_.items()) {
      /* TODO: possibly just use a char. */
      std::string data_message_string = data_message;

      send_message(data_message_string.c_str(),
                   int(data_message_string.size()),
                   item.value,
                   important,
                   nullptr);
    }
  }
  else /* not host. */ {
    /* First send to host which will send to all. */
    char data_message[MAXPACKAGESIZE] = "-ToAll ";
    MU_prepare_package(package_name.c_str(), data_message + strlen(data_message));

    if (them_addrss_.size() > 0) {
      std::string data_message_string = data_message;

      /* Send the message to host. */
      send_message(data_message_string.c_str(),
                   int(data_message_string.size()),
                   them_addrss_.lookup("HOST"),
                   important,
                   nullptr);
    }
  }
}

void Dance::MU_send_package_to(std::string package_name, int them_ID, bool to_host, bool important)
{
  char data_message[MAXPACKAGESIZE];
  MU_prepare_package(package_name.c_str(), data_message);
  MU_send_message_to(data_message, them_ID, to_host, false, important);
}

void Dance::MU_send_to_self(std::string package_name)
{
  message_vector_mutex_.lock();
  char data_message[MAXPACKAGESIZE];
  MU_prepare_package(package_name.c_str(), data_message);
  received_messages_.insert(received_messages_.begin(), data_message);
  message_vector_mutex_.unlock();
}

void Dance::MU_prepare_package(const char *package_name, char *r_data_message)
{
  /* Get the data ready. */
  int offset = 0;
  memcpy(r_data_message + offset, name_.c_str(), name_.length());
  offset += int(name_.length());
  std::memcpy(r_data_message + offset, ": ", 2);
  offset += 2;
  r_data_message[offset++] = ' ';
  memcpy(r_data_message + offset, package_name, std::strlen(package_name));
  offset += int(std::strlen(package_name));
  r_data_message[offset++] = ' ';

  const auto &package = package_map_.lookup(package_name);
  const int size = int(package.size());

  /* Loop over all the data and add it to the message. */
  for (int i = 0; i < size; i++) {
    /* Get value. */
    const std::variant<int, float, uint32_t, const char *, std::string, double, bool> &value =
        package[i];

    /* Cast from correct type to string. */
    if (auto value_int = std::get_if<int>(&value)) {
      offset += snprintf(r_data_message + offset, MAXPACKAGESIZE - offset, "%i ", *value_int);
    }
    else if (auto value_unsigned = std::get_if<uint32_t>(&value)) {
      offset += snprintf(r_data_message + offset, MAXPACKAGESIZE - offset, "%i ", *value_unsigned);
    }
    else if (auto value_double = std::get_if<double>(&value)) {
      offset += snprintf(r_data_message + offset, MAXPACKAGESIZE - offset, "%f ", *value_double);
    }
    else if (auto value_float = std::get_if<float>(&value)) {
      offset += snprintf(r_data_message + offset, MAXPACKAGESIZE - offset, "%f ", *value_float);
    }
    else if (auto value_string = std::get_if<std::string>(&value)) {
      memcpy(r_data_message + offset, value_string->c_str(), value_string->length());
      offset += int(value_string->length());
      r_data_message[offset++] = ' ';
    }
    else if (auto value_char = std::get_if<const char *>(&value)) {
      memcpy(r_data_message + offset, value_char, std::strlen(*value_char));
      offset += int(std::strlen(*value_char));
      r_data_message[offset++] = ' ';
    }
    else if (auto value_bool = std::get_if<bool>(&value)) {
      offset += snprintf(
          r_data_message + offset, MAXPACKAGESIZE - offset, "%s ", *value_bool ? "true" : "false");
    }
    /* Message too big, report it and return. */
    if (offset > MAXPACKAGESIZE - 10) {
      /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, "Error, message size too big"); */
      return;
    }
  }
  offset += snprintf(r_data_message + offset, MAXPACKAGESIZE - offset, "%s", "end");

  return;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \Other helper functions
 * \{ */

addrinfo *Dance::storage_to_addr_info(sockaddr_storage input, int input_size)
{
  addrinfo *res;

  /* If using the same decive, do not store the info as IP. */
  if (current_moves_ == SAMEDEVICE) {
    /* Malloc memory for #res. */
    res = reinterpret_cast<addrinfo *>(malloc(sizeof(addrinfo)));

    if (res == NULL) {
      /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, "Malloc failed"); */
      return nullptr;
    }
    /* Manually copy the adress. */
    res->ai_family = input.ss_family;
    res->ai_socktype = SOCK_DGRAM;

    res->ai_addr = reinterpret_cast<struct sockaddr *>(malloc(input_size));
    if (res->ai_addr == NULL) {
      /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, "Malloc failed"); */
      return nullptr;
    }
    memcpy(res->ai_addr, &input, input_size);
    res->ai_addrlen = input_size;
  }
  else {
    struct addrinfo hints;
    int status;
    char address_struct[INET6_ADDRSTRLEN] = {0}; /* This will hold the IP. */
    if (input.ss_family == AF_INET) {            /* #IPV4 */
      /* Get IP of other. */
      struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>(&input);
      inet_ntop(AF_INET, &ipv4->sin_addr, address_struct, sizeof(address_struct));
    }
    else { /* #IPV6 */
      /* Get IP of other. */
      struct sockaddr_in6 *ipv6 = reinterpret_cast<struct sockaddr_in6 *>(&input);
      inet_ntop(AF_INET6, &ipv6->sin6_addr, address_struct, sizeof(address_struct));
    }

    /* Handle this and put it inside the map */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = input.ss_family;
    hints.ai_socktype = SOCK_DGRAM;
    /* #addrinfo */
    if ((status = getaddrinfo(address_struct, host_port_, &hints, &res)) != 0) {
      char error_buffer[256];
      sprintf_s(error_buffer,
                sizeof(error_buffer),
                "getaddrinfo error on clients IP: %s",
                get_send_errors(WSAGetLastError()));
      /*LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, error_buffer); */
    }
  }

  return res;
}

const char *Dance::get_send_errors(int error_code)
{
  const char *output;

  switch (error_code) {
    case 10022:
      output = "Invalid Argument (10022)\n";
      break;
    case 10038:
      output = "Socket was invalid\n";
      break;
    case 10048:
      output = "Address already in use\n";
      break;
    case 10050:
      output = "Network is down\n";
      break;
    case 10051:
      output = "Network is unreachable\n";
      break;
    case 10054:
      output = "Connection reset by peer\n";
      break;
    case 10060:
      output = "Connection Timed Out\n";
      break;
    case 10061:
      output = "Connection Refused\n";
      break;
    default:
      char buffer[256];

      snprintf(buffer,
               256,
               "(errorCode: %i not implemented, view "
               "https://learn.microsoft.com/en-us/windows/win32/winsock/"
               "windows-sockets-error-codes-2 for more information\n",
               error_code);
      output = buffer;
      break;
  }
  return output;
}

/** \} */
