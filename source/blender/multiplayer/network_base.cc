#include "network_base.hh"

// Networking classes
#include <iphlpapi.h>  //For retrieving private IP
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

//---------------------------------------------------------------------------------------
//  Code was made using help from https://beej.us/guide/bgnet/pdf/bgnet_usl_c_1.pdf
//---------------------------------------------------------------------------------------

Dance::~Dance()
{
  // If still connections, tell that we disconnected
  const char *message = "Disconnect";
  for (const auto &pair : them_addrss) {
    if (sendto(sockfd,
               message,
               int(strlen(message)),
               0,
               pair.second->ai_addr,
               int(pair.second->ai_addrlen)) == -1)
    {
      char errorBuffer[256];
      sprintf_s(
          errorBuffer, sizeof(errorBuffer), "Send failed %s", getSendError(WSAGetLastError()));
      //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);
    }
  }

  // Close socket so that recvfrom() will end
  shutdown(sockfd, SD_BOTH);
  closesocket(sockfd);
  // Wait for thread
  if (ConnectionMade.valid()) {
    QuitListening = true;
    QuitCallBack = true;
    ConnectionMade.get();  // Waits for the thread to finish execution
  }
  if (listenThread.joinable()) {
    QuitListening = true;
    listenThread.join();
  }
  if (callBackThread.joinable()) {
    QuitCallBack = true;
    callBackThread.join();
  }
  them_numbers.clear();
  them_addrss.clear();
}

void Dance::init(bool useCallBack, bool _ForceIPV4)
{
  // First pass the version of winsock
  // Latest version is 2.2, this one we want to use.
  WSADATA wsaData;

  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    //LOGLINE(DLogObj, DanceLogger::DANCE_CRITICAL, "WSAStartup failed");
    exit(1);
  }
  if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
    WSACleanup();  // We are done with winsock
    //LOGLINE(DLogObj, DanceLogger::DANCE_CRITICAL, "Version 2.2 of winsock is not available");
    exit(2);
  }

  UseCallBackFunctions = useCallBack;
  forceIPV4 = _ForceIPV4;
}

//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
//-----------------------------------      Connecting devices to eachother
//--------------------------------------
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------

void Dance::Host(DanceMoves moves,
                 int maxConnections,
                 const char *port,
                 bool _forceIPV4,
                 const std::vector<std::string> &clientsIP)
{
  /*
  --------------------------------------Set Variables--------------------------------
  */

  forceIPV4 = _forceIPV4;
  CurrentMoves = moves;
  host = true;
  MaxConnections = maxConnections;

  // Check if the inserted port is valid, otherwise just use the HOSTPORT
  int testPort = 0;
  if (port != NULL)
    testPort = atoi(port);
  if (testPort <= 1024 || testPort >= 49151) {
    port = HOSTPORT;
    char errorBuffer[256];
    sprintf_s(errorBuffer, sizeof(errorBuffer), "Default port: %s is used", HOSTPORT);
    //LOGLINE(DLogObj, DanceLogger::DANCE_INFO, errorBuffer);
  }
  else {
    memcpy_s(HOSTPORT, sizeof(HOSTPORT), port, strlen(port));
  }

  /*
  -----------------------------------------Set IPs-----------------------------------
  */

  char otherIP[39] = " ";
  char ownIP[39] = " ";

  // If we are on the same device, we just use 'localhost' as our IP.
  if (moves == SAMEDEVICE) {
    // Use strcpy_s to set otherIP to localhost
    const char *localHostInput = "localhost";
    strcpy_s(otherIP, localHostInput);
    strcpy_s(ownIP, localHostInput);
    ipv = AF_INET;
  }
  else {
    bool p = false;
    if (moves == PUBLIC) {
      p = true;
    }
    strcpy_s(ownIP, getIP(p).c_str());  // Set ownIP using getIP()
  }

  /*
  -----------------------------------------Important variables-----------------------------------
  */

  struct addrinfo hints, *res;
  int status;
  const char yes = '1';

  /*
  --------------------------------------Add client IP to
  connections--------------------------------
  */

  if (moves == PUBLIC) {
    // Put all the clients IPs in the map
    for (int i = 0; i < int(clientsIP.size()); i++) {
      strcpy_s(otherIP, clientsIP[i].c_str());

      memset(&hints, 0, sizeof hints);
      hints.ai_family = ipv;  // Use IPV4 or IPV6
      hints.ai_socktype = SOCK_DGRAM;
      // addrinfo
      if ((status = getaddrinfo(otherIP, HOSTPORT, &hints, &res)) != 0) {
        char errorBuffer[256];
        sprintf_s(errorBuffer,
                  sizeof(errorBuffer),
                  "(Invalid IP) getaddrinfo error on clients IP %s",
                  getSendError(WSAGetLastError()));
        //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
        continue;
      }
      // Get name of ourself
      char nameInfoHost[256];
      char nameInfoServ[256];
      // TODO: if IP is wrong this can take ages. (sometimes seconds)
      getnameinfo(res->ai_addr,
                  int(res->ai_addrlen),
                  nameInfoHost,
                  sizeof(nameInfoHost),
                  nameInfoServ,
                  sizeof(nameInfoServ),
                  0);

      // Add host to map
      them_addrss.emplace(std::make_pair(nameInfoHost, res));
      them_numbers.emplace(std::make_pair(nameInfoHost, 0));
      // Don't Increase total connections (We do this after we confirmed the connection in
      // holepunching) TotalConnections++;
    }
  }

  /*
  -------------------------------------------Winsock2 Code---------------------------------------
  */

  // Get Address information for this device
  res = nullptr;
  memset(&hints, 0, sizeof hints);  // Reset Hints
  hints.ai_family = ipv;            // Use IPV4 or IPV6
  hints.ai_socktype = SOCK_DGRAM;   // Use UPD

  // If we are binding using ipv4, we use our private IP
  char ownIPV2[65];
  if (moves == PUBLIC && ipv == AF_INET) {
    strcpy_s(ownIPV2, getIP(false).c_str());
  }
  else {
    strcpy_s(ownIPV2, ownIP);
  }

  // Get address info of the host (ourself)
  if ((status = getaddrinfo(ownIP, HOSTPORT, &hints, &res)) != 0) {
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "getaddrinfo error: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    return;
  }

  // Get name of ourself
  char nameInfoHost[256];
  char nameInfoServ[256];
  getnameinfo(res->ai_addr,
              int(res->ai_addrlen),
              nameInfoHost,
              sizeof(nameInfoHost),
              nameInfoServ,
              sizeof(nameInfoServ),
              0);
  DeviceName = nameInfoHost;

  // Create Socket
  if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == INVALID_SOCKET) {
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "Error on socket creation: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    return;
  }

  // Loose the "socket already in use" error
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    char errorBuffer[256];
    sprintf_s(
        errorBuffer, sizeof(errorBuffer), "setsockopt error: %s", getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);
    return;
  }

  // Bind
  if (bind(sockfd, res->ai_addr, int(res->ai_addrlen)) == -1) {
    char errorBuffer[256];
    sprintf_s(
        errorBuffer, sizeof(errorBuffer), "Error on binding: %s", getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    return;
  }

  freeaddrinfo(res);  // Free the data.

  /*
  ---------------------------------------Wait for Connections-------------------------------------
  */

  // If name is "NONE", use the device name or in the case of the same device, use program ID
  if (name == "NONE")
    name = DeviceName;
  // IGNORE ALL ABOVE LETS CALL US 'HOST'
  name = "HOST";
  // This we do so we can indentify if we are the host, but maybe this will get removed later.

  // If we use a public network, we need to hole punch
  if (moves == DanceMoves::PUBLIC) {
    if (listenThread.joinable()) {
      listenThread.join();
    }
    if (callBackThread.joinable()) {
      callBackThread.join();
    }

    // Used as listenThread, since this will transform into listening after its done
    listenThread = std::thread([this] { this->holePunch(); });
    return;
  }

  // First check and possibly clean up old mess.
  if (listenThread.joinable()) {
    listenThread.join();
  }
  if (callBackThread.joinable()) {
    callBackThread.join();
  }

  // Now we just need to wait for incomming connections.
  // Create a thread that will listen for a return message, this will add connections to our map of
  listenThread = std::thread([this] { this->listen(true); });
  // Another one for the call backs
  if (UseCallBackFunctions)
    callBackThread = std::thread([this] { this->sendCallBacks(); });
}

bool Dance::Connect(DanceMoves moves, const char *hostIP, const char *port, bool _forceIPV4)
{
  /*
  --------------------------------------Set Variables--------------------------------
  */
  char otherIP[39] = " ";
  char ownIP[39] = " ";

  forceIPV4 = _forceIPV4;
  CurrentMoves = moves;
  host = false;
  strcpy_s(otherIP, hostIP);

  // Check if the inserted port is valid, otherwise just use the HOSTPORT
  int testPort = 0;
  if (port != NULL)
    testPort = atoi(port);
  if (testPort <= 1024 || testPort >= 49151) {
    port = HOSTPORT;
    char errorBuffer[256];
    sprintf_s(errorBuffer, sizeof(errorBuffer), "Default port: %s is used", HOSTPORT);
    //LOGLINE(DLogObj, DanceLogger::DANCE_INFO, errorBuffer);
  }
  else {
    memcpy_s(HOSTPORT, sizeof(HOSTPORT), port, strlen(port));
  }

  /*
  -----------------------------------------Set IPs-----------------------------------
  */

  // If we are on the same device, we just use 'localhost' as our IP.
  if (moves == SAMEDEVICE) {
    // Use strcpy_s to set otherIP to localhost
    const char *localHostInput = "localhost";
    strcpy_s(otherIP, localHostInput);
    strcpy_s(ownIP, localHostInput);
    ipv = AF_INET;  // Set to IPV4
  }
  else {
    bool p = false;
    if (moves == PUBLIC)
      p = true;
    strcpy_s(ownIP, getIP(p).c_str());  // Set ownIP using getIP()
  }

  /*
  -----------------------------------------Important variables-----------------------------------
  */

  struct addrinfo hints, *res, *dest;
  int status;

  /*
  --------------------------------------Add host IP to connections--------------------------------
  */

  // Handle this and put it inside the map
  memset(&hints, 0, sizeof hints);
  hints.ai_family = ipv;
  hints.ai_socktype = SOCK_DGRAM;
  // addrinfo
  if ((status = getaddrinfo(otherIP, HOSTPORT, &hints, &res)) != 0) {
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "getaddrinfo error on host: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    resetState();
    return false;
  }

  // Add host to map
  them_addrss.emplace(std::make_pair("HOST", res));
  them_numbers.emplace(std::make_pair("HOST", 0));

  /*
  --------------------------------------Get Address information for this
  device--------------------------------
  */

  res = nullptr;
  memset(&hints, 0, sizeof hints);  // Reset Hints
  hints.ai_family = ipv;            // Use IPV4 or IPV6
  hints.ai_socktype = SOCK_DGRAM;   // Use UPD
  hints.ai_protocol = IPPROTO_UDP;

  // If we are binding using ipv4, we use our private IP
  char ownIPV2[65];
  if (moves == PUBLIC && ipv == AF_INET) {
    strcpy_s(ownIPV2, getIP(false).c_str());
  }
  else {
    strcpy_s(ownIPV2, ownIP);
  }

  // Get address info of ourself
  if ((status = getaddrinfo(ownIPV2, HOSTPORT, &hints, &res)) != 0) {
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "getaddrinfo error: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    resetState();
    return false;
  }

  // Get name of ourself
  char nameInfoHost[256];
  char nameInfoServ[256];
  getnameinfo(res->ai_addr,
              int(res->ai_addrlen),
              nameInfoHost,
              sizeof(nameInfoHost),
              nameInfoServ,
              sizeof(nameInfoServ),
              0);
  DeviceName = nameInfoHost;

  /*
  --------------------------------------Create Socket and possibly
  bind--------------------------------
  */

  // Create Socket
  if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == INVALID_SOCKET) {
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "Error on socket creation: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    resetState();
    return false;
  }

  // Bind only if we use public IP, we dont need it elsewhere
  if (moves == PUBLIC) {
    if (bind(sockfd, res->ai_addr, int(res->ai_addrlen)) == -1) {
      char errorBuffer[256];
      sprintf_s(errorBuffer,
                sizeof(errorBuffer),
                "Error on binding: %s",
                getSendError(WSAGetLastError()));
      //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
      resetState();
      return false;
    }
  }

  freeaddrinfo(res);  // Free the data. (We don't need to use it anymore)

  /*
  ---------------------------------------------Connect To
  Host---------------------------------------------------
  */

  // If name is "NONE", use the device name or in the case of the same device, use program ID
  if (moves == SAMEDEVICE && name == "NONE")
    name = std::to_string(GetCurrentProcessId());
  else if (name == "NONE")
    name = DeviceName;

  // If we use a public network, we need to hole punch
  if (moves == PUBLIC) {
    if (listenThread.joinable()) {
      listenThread.join();
    }
    if (callBackThread.joinable()) {
      callBackThread.join();
    }

    // Used as listenThread, since this will transform into listening after its done
    listenThread = std::thread([this] { this->holePunch(); });

    return true;
  }

  // Send a message to the host to connect
  // message Structure: 'Con' + 'name of the device'
  std::string connectMessage = "Con";
  connectMessage += name;

  // Get address info of the host
  if ((status = getaddrinfo(otherIP, HOSTPORT, &hints, &dest)) != 0) {
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "getaddrinfo error: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    resetState();
    return false;
  }

  // Connect for windows to send the error message to this socket, otherwise it may not does this
  if (connect(sockfd, dest->ai_addr, int(dest->ai_addrlen)) != 0) {
    char errorBuffer[256];
    sprintf_s(
        errorBuffer, sizeof(errorBuffer), "connection error: %s", getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);
    resetState();
    return false;
  }

  // Using send (not sendto), because we are connected and have set up a default location
  if (send(sockfd, connectMessage.c_str(), int(connectMessage.size()), 0) == -1) {
    char errorBuffer[256];
    sprintf_s(
        errorBuffer, sizeof(errorBuffer), "Send failed: %s", getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);
    resetState();
    return false;
  }

  Sleep(50);  // Wait for package to arrive safely
  char buffer[MAXPACKAGESIZE];
  int result = 0;

  // Unblock socket
  u_long iMode = 1;
  if (ioctlsocket(sockfd, FIONBIO, &iMode) != 0) {
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "Could not unblock socket: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);
    resetState();
    return false;
  }

  if ((result = recv(sockfd, buffer, MAXPACKAGESIZE, MSG_PEEK)) <
      0)  // We can safely consume this message, since this is a conConfirm.
  {
    if (WSAGetLastError() == 10054)  // Connection reset by peer.
    {
      //LOGLINE(DLogObj,
      //        DanceLogger::DANCE_WARNING,
      //        "Could not connect to socket, is the host connected?");
      resetState();
      return false;
    }
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "Receive error trying to connect, did the host lag out?: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    resetState();
    return false;
  }

  // Block socket again
  iMode = 0;
  if (ioctlsocket(sockfd, FIONBIO, &iMode) != 0) {
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "Could not block socket: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);
    resetState();
    return false;
  }

  /*
  --------------------------------------Create Threads---------------------------------------
  */

  // First check and possibly clean up old mess.
  if (listenThread.joinable()) {
    listenThread.join();
  }
  if (callBackThread.joinable()) {
    callBackThread.join();
  }

  // Now create a thread that we will listen on indeffinitely
  listenThread = std::thread([this] { this->listen(true); });

  // Another one for the call backs
  if (UseCallBackFunctions)
    callBackThread = std::thread([this] { this->sendCallBacks(); });

  return true;
}

void Dance::holePunch()
{
  HolePunchingStatus = 1;

  // Generate seed
  unsigned seed = int(std::chrono::system_clock::now().time_since_epoch().count());
  // Use this seed to get random numbers
  srand(seed);

  // Create vector of all connections we need to make
  std::vector<struct addrinfo *> futureConnections;
  {
    std::map<std::string, struct addrinfo *>::iterator it;
    for (it = them_addrss.begin(); it != them_addrss.end(); it++)
      futureConnections.push_back(it->second);
  }

  // While-loop until we connected to everyone
  while (futureConnections.size() > 0) {
    // Thread to listen
    ConnectionMade = std::async(std::launch::async, [this] { return this->listen(false); });

    while (true) {
      auto status = ConnectionMade.wait_for(std::chrono::milliseconds(100));
      if (status == std::future_status::ready) {
        if (ConnectionMade.get())
          break;  // Connection was successful
        // Note that it is possible for status to be true, but we already had them, so we did not
        // progress.
      }

      // Sending message
      std::string tinyMessage = "HolePunch";
      tinyMessage = "Con " + name + ": " + tinyMessage;

      // If we succefully connected on the first iteration, it will finish the other 9 before
      // realizing
      for (int j = 0; j < 10; j++) {
        // Iterate over all the other peers
        for (int i = 0; i < int(futureConnections.size()); i++) {
          if (sendto(sockfd,
                     tinyMessage.c_str(),
                     int(tinyMessage.size()),
                     0,
                     futureConnections[i]->ai_addr,
                     int(futureConnections[i]->ai_addrlen)) == -1)
          {
            char errorBuffer[256];
            sprintf_s(errorBuffer,
                      sizeof(errorBuffer),
                      "Send failed: %s",
                      getSendError(WSAGetLastError()));
            //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);
          }
        }
        // Sleep for between 0.09 and 0.11 seconds
        Sleep(rand() % 2 + 9);
      }
    }

    // Check which one connected and remove it from the vector
    for (int i = 0; i < int(HolePunchConfirmedConnections.size()); i++) {
      for (int j = 0; j < int(futureConnections.size()); j++) {
        if (HolePunchConfirmedConnections[i]->ai_addrlen == futureConnections[j]->ai_addrlen &&
            memcmp(HolePunchConfirmedConnections[i]->ai_addr,
                   futureConnections[j]->ai_addr,
                   HolePunchConfirmedConnections[i]->ai_addrlen) == 0)
        {
          // remove from futureconnections
          futureConnections.erase(futureConnections.begin() + j);
        }
      }
    }
  }
  // Clear vector, we dont need it anymore
  HolePunchConfirmedConnections.clear();
  HolePunchingStatus = 2;

  // Create a thread for callback
  if (UseCallBackFunctions)
    callBackThread = std::thread([this] { this->sendCallBacks(); });

  // Now start listening until stopped
  listen(true);
}

void Dance::KeepAlive(float dt)
{
  if (CurrentMoves == PUBLIC && host) {
    KeepAliveTime -= dt;
    if (KeepAliveTime <= 0.0f) {
      KeepAliveTime = 60.0f;

      const char *message = "KeepAliveMessage";
      // Send message to everyone.
      for (const auto &pair : them_addrss) {
        // Send the message
        if (sendto(sockfd,
                   message,
                   int(strlen(message)),
                   0,
                   pair.second->ai_addr,
                   int(pair.second->ai_addrlen)) == -1)
        {
          char errorBuffer[256];
          sprintf_s(errorBuffer,
                    sizeof(errorBuffer),
                    "Send failed: %s",
                    getSendError(WSAGetLastError()));
          //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);
        }
      }
    }
  }
  else  // Send to everyone a garbage message
  {
    QuickKeepAliveTime -= dt;
    if (QuickKeepAliveTime <= 0.0f) {
      QuickKeepAliveTime = 0.5f;

      const char *message = "KeepAliveMessage";
      // Send message to everyone.
      for (const auto &pair : them_addrss) {
        // Send the message
        if (sendto(sockfd,
                   message,
                   int(strlen(message)),
                   0,
                   pair.second->ai_addr,
                   int(pair.second->ai_addrlen)) == -1)
        {
          char errorBuffer[256];
          sprintf_s(errorBuffer,
                    sizeof(errorBuffer),
                    "Send failed: %s",
                    getSendError(WSAGetLastError()));
          //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);
        }
      }
    }
  }
}

void Dance::getPackage(std::string PackageName, bool DeleteMessage, ReturnPackageInfo *packageInfo)
{
  MessageVectorMutex.lock();

  bool isList = false;
  for (int i = int(ReceivedMessages.size()) - 1; i >= 0; --i) {
    bool gotName = false;
    std::string tmp;
    int wordIndex = 0;
    packageInfo->succeeded = true;

    while (!(tmp = getWord(ReceivedMessages[i], wordIndex++)).empty()) {
      // First index is the name
      if (!gotName) {
        gotName = true;
        continue;  // Can use later
      }

      // Check if this is the list we are searching for
      if (!isList) {
        if (tmp != PackageName)
          break;
        else {
          isList = true;
          continue;  // Go to next word
        }
      }

      if (tmp == "end")
        break;

      bool parsed = false;

      std::apply(  // Apply unpacks the tuple arguments into the lamda so types will become the
                   // types in the tuple.
          [&](auto... types)  // lambda takes a reference pack of all the tuple types.
          {
            (... ||
             (parsed = tryParse(tmp,
                                types),  // Try to parse each empty type, parsed will be true or
                                         // false, types will be filled with input if succeeded
              parsed ?
                  (packageInfo->VariableVector.push_back(types), true) :
                  false));  // If parsed, push back the type and return true, else return false.
          },
          std::tuple<int, float, double, unsigned, bool, std::string>{});  // Types that we will
                                                                           // test

      if (!parsed) {
        // Just insert the string
        packageInfo->VariableVector.push_back(tmp);
        // Let know that not all variables succeeded to parse
        packageInfo->succeeded = false;
      }
    }
    // Gathered all variables (hopefully)
    if (DeleteMessage && packageInfo->succeeded && isList) {
      ReceivedMessages.erase(ReceivedMessages.begin() + i);  // Delete this message if we want to.
    }
    if (isList)
      break;
  }
  if (!isList)
    packageInfo->succeeded = false;
  MessageVectorMutex.unlock();
}

//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
//-----------------------------------      Listen for incomming messages
//----------------------------------------
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------

bool Dance::listen(bool keepChecking)
{
  const int MaxMessagesVectorSize = 100;
  struct sockaddr_storage them_addr{};  // Adress of the others
  char buffer[MAXPACKAGESIZE]{};

  // Up the writing size and reading size
  int sendBufferSize = 2 * 1024;   // 2 KB send buffer size
  int recvBufferSize = 32 * 1024;  // 32 KB receive buffer size
  if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char *)&sendBufferSize, sizeof(sendBufferSize)) <
      0)
  {
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "Error setting send buffer size: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);
  }
  // Set receive buffer size
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char *)&recvBufferSize, sizeof(recvBufferSize)) <
      0)
  {
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "Error setting receive buffer size: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);
  }

  // Set current time based on time since program started
  lastTime = static_cast<unsigned long long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  QuitListening = false;
  while (!QuitListening) {
    socklen_t addr_size = sizeof(sockaddr_storage);
    int len = recvfrom(
        sockfd, (char *)buffer, sizeof(buffer), 0, (struct sockaddr *)&them_addr, &addr_size);
    if (len < 0) {
      // Highly likely that the connection was reset by peer, if not, check it out!
      bool removedPeer = handleDisconnection(them_addr, addr_size);

      if (removedPeer && !host)  // TODO: what if Public connection
      {
        QuitListening = true;
        listenThread.detach();
        return false;
      }
      if (!removedPeer) {
        char errorBuffer[256];
        sprintf_s(errorBuffer,
                  sizeof(errorBuffer),
                  "Error receiving message: %s",
                  getSendError(WSAGetLastError()));
        //LOGLINE(DLogObj, DanceLogger::DANCE_INFO, errorBuffer);
      }
    }
    else if (len == 0) {
      char errorBuffer[256];
      sprintf_s(errorBuffer,
                sizeof(errorBuffer),
                "Buffer of 0 received, connection closed?: %s",
                getSendError(WSAGetLastError()));
      //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    }
    else {
      // Check buffer size DEBUG
      // u_long bytesAvailable;
      // if (ioctlsocket(sockfd, FIONREAD, &bytesAvailable) == 0)
      //{
      //     std::cout << "Bytes available in the receive buffer: " << bytesAvailable << std::endl;
      // }
      // else
      //{
      //     perror("Error querying receive buffer size\n");
      // }

      if (len >= MAXPACKAGESIZE) {
        char errorBuffer[256];
        sprintf_s(errorBuffer,
                  sizeof(errorBuffer),
                  "Message too long: %s",
                  getSendError(WSAGetLastError()));
        //LOGLINE(DLogObj, DanceLogger::DANCE_INFO, errorBuffer);
        continue;
      }

      // Received message
      // printf("Message received: %s\n", buffer);

      bool ConMessage = false;
      std::string otherName{};
      buffer[len] = '\0';  // Null terminate the received message

      //---------------------------------------------------Important
      //Message?-------------------------------------------

      // Check if it is an important message
      if (std::strncmp(buffer, "Imp", 3) == 0) {
        // Remove 'Imp ' from the buffer
        memcpy_s(buffer, sizeof(buffer), &buffer[3], sizeof(buffer) - 3);
        std::string buf(buffer);
        std::string ID = std::string(getWord(buf, 0));
        // Remove ID from buffer
        memcpy_s(buffer,
                 sizeof(buffer),
                 &buffer[ID.length() + 1],
                 sizeof(buffer) - (ID.length() + 1));  // TODO: test

        // Send back that it succeeded
        addrinfo *res = storageToAddrInfo(them_addr, addr_size);
        const std::string impReturnMessage = "SImp" + ID;  // Succes Important

        if (sendto(sockfd,
                   impReturnMessage.c_str(),
                   int(strlen(impReturnMessage.c_str())),
                   0,
                   res->ai_addr,
                   int(res->ai_addrlen)) == -1)
        {
          char errorBuffer[256];
          sprintf_s(errorBuffer,
                    sizeof(errorBuffer),
                    "Send failed: %s",
                    getSendError(WSAGetLastError()));
          //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
        }
      }

      // Check if it is a succes important message
      if (std::strncmp(buffer, "SImp", 4) == 0) {
        // Get ID
        // Remove 'SImp' from the buffer
        memcpy_s(buffer, sizeof(buffer), &buffer[4], sizeof(buffer) - 4);
        // Remove from array
        ImportantSendMessages.erase(atoi(buffer));
        continue;
      }

      // Check on the other important messages in array
      if (!ImportantSendMessages.empty()) {
        const unsigned long long ms = static_cast<unsigned long long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        const float dt = static_cast<float>(ms - lastTime) * 0.001f;
        lastTime = ms;
        timeCheckedImp += dt;

        // Did enough time passed?
        if (timeCheckedImp > 0.1f) {
          timeCheckedImp = 0.0f;

          for (auto &it : ImportantSendMessages) {
            if (it.second.checksDone >= 5) {
              // Probably do something
              char errorBuffer[256];
              sprintf_s(errorBuffer,
                        sizeof(errorBuffer),
                        "Never received important message confirmation from message: %s",
                        it.second.message.c_str());
              //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
            }
            else  // Send again
            {
              // Get userID
              int userID = static_cast<int>(
                  static_cast<float>(it.second.ID) /
                  std::powf(10.0f, std::floorf(std::log10(static_cast<float>(it.second.ID)))));
              // TODO: is userID correct?

              char errorBuffer[256];
              sprintf_s(errorBuffer,
                        sizeof(errorBuffer),
                        "Needed to send this important message a second time: %s",
                        it.second.message.c_str());
              //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, errorBuffer);

              const std::string impReturnMessage = "Imp" + std::to_string(it.second.ID) + " " +
                                                   it.second.message;  // Succes Important

              for (const auto &pair : them_addrss) {
                if (them_numbers.at(pair.first) == userID)  // Send to this one
                {
                  if (sendto(sockfd,
                             impReturnMessage.c_str(),
                             int(strlen(impReturnMessage.c_str())),
                             0,
                             pair.second->ai_addr,
                             int(pair.second->ai_addrlen)) == -1)
                  {
                    char errorBuffer[256];
                    sprintf_s(errorBuffer,
                              sizeof(errorBuffer),
                              "Send failed: %s",
                              getSendError(WSAGetLastError()));
                    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
                  }
                }
              }

              it.second.checksDone++;
            }
          }
        }
      }

      //---------------------------------------------------Connection
      //Message?-------------------------------------------

      // Check if this is a connection message
      if (std::strncmp(buffer, "Con", 3) == 0) {
        // If it is a ConConfirm, and we don't want to keep checking, we can return
        if (std::strncmp(buffer, "ConConfirm", 10) == 0) {
          if (keepChecking) {
            continue;
          }
          else {
            return true;
          }
        }
        // Check if it is above our max connections
        if (them_addrss.size() >= MaxConnections)
          continue;
        ConMessage = true;
        otherName = &buffer[3];  // Name starts after the 'Con'
      }

      // Check if it is a disconnect message
      else if (std::strncmp(buffer, "Dis", 3) == 0) {
        if (handleDisconnection(them_addr, addr_size)) {
          if (!host) {
            QuitListening = true;
            return false;
          }
          else {
            continue;
          }
        }
      }

      //---------------------------------------------------Amount of connections
      //Message?-------------------------------------------

      // Check if this is a message to check how many connections we have (non-host only)
      else if (!host && std::strncmp(buffer, "Tot", 3) == 0) {
        // Check for certainty if it really is this message
        if (std::strncmp(buffer, "TotalConCount", 13) == 0) {
          // Update total connection count
          std::string buf(buffer);
          TotalConnections = std::stoi(getWord(buf, 1).data());
          continue;
        }
      }

      //---------------------------------------------------Package-Style
      //Message?-------------------------------------------

      // Check if it is a sendTo message, if so, we know we are the host
      else if (std::strncmp(buffer, "-To", 3) == 0) {
        // Send to another client
        if (std::strncmp(buffer, "-ToCl-", 6) == 0) {
          // Support for 99 users
          int extra = 0;
          std::string ID = std::to_string(buffer[6]);
          if (buffer[7] != ' ') {
            ID += std::to_string(buffer[7]);
            extra++;
          }
          std::string message = &buffer[8 + extra];
          sendMessageTo(message, std::stoi(ID), 0, false);
          continue;  // We dont want to do anything with this message so move on.
        }
        // Handle it like a normal message but remove the first part
        else if (host && std::strncmp(buffer, "-ToHo", 5) == 0) {
          // Fill in the name
          memcpy_s(buffer, sizeof(buffer), &buffer[7], sizeof(buffer) - 7);
          int i = 0;
          while (buffer[i] != ':')
            otherName += buffer[i++];
        }
        // Send to all, including us but not to the sender
        else if (std::strncmp(buffer, "-ToAll", 6) == 0) {
          std::string message = &buffer[7];

          std::map<std::string, int>::iterator it;
          for (it = them_numbers.begin(); it != them_numbers.end(); it++) {
            if (it->second != 0)  // Don't send to host (ourself)
            {
              sendMessageTo(message, it->second, 0, false);
            }
          }
          // Now handle it ourself

          //  Fill in the name
          memcpy_s(buffer, sizeof(buffer), &buffer[7], sizeof(buffer) - 7);
          int i = 0;
          while (buffer[i] != ':')
            otherName += buffer[i++];
        }
      }

      if (otherName.empty()) {
        int i = 0;
        while (i < strlen(buffer) && buffer[i] != ':')
          otherName += buffer[i++];
      }

      // If we are on the same device, we need to check if we are on the same program or not
      if (CurrentMoves == SAMEDEVICE && name == otherName) {
        // we are the one sending the message, so ignore
        // printf("Ignored above message\n");
        continue;
      }

      // Store package to queue
      if (UseCallBackFunctions) {
        CallBackMutex.lock();
        UserPackageStorage.push(buffer);
        CallBackMutex.unlock();
      }

      // Store message
      MessageVectorMutex.lock();
      ReceivedMessages.push_front(buffer);
      // Delete oldest message if vector is too big
      if (ReceivedMessages.size() > MaxMessagesVectorSize) {
        char errorBuffer[256];
        sprintf_s(errorBuffer,
                  sizeof(errorBuffer),
                  "Message getting deleted due to being full: %s",
                  ReceivedMessages.back().c_str());
        //LOGLINE(DLogObj, DanceLogger::DANCE_INFO, errorBuffer);
        ReceivedMessages.pop_back();
      }
      MessageVectorMutex.unlock();

      if (!ConMessage)
        continue;  // Only do this next part if it is a connection message

      //---------------------------------------------------Do we have them
      //already?-------------------------------------------

      // Convert from storage to addrinfo.
      addrinfo *res = storageToAddrInfo(them_addr, addr_size);

      // Send a confirm that the message reached us succesfully
      std::string ConfirmConMessage = "ConConfirm";
      if (sendto(sockfd,
                 ConfirmConMessage.c_str(),
                 int(ConfirmConMessage.size()),
                 0,
                 res->ai_addr,
                 int(res->ai_addrlen)) == -1)
      {
        char errorBuffer[256];
        sprintf_s(
            errorBuffer, sizeof(errorBuffer), "Send failed: %s", getSendError(WSAGetLastError()));
        //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
      }

      // Now check if we already had this client
      bool gotThem = false;
      std::map<std::string, struct addrinfo *>::iterator it;
      for (it = them_addrss.begin(); it != them_addrss.end(); it++) {
        // Use 2 ways to check, one with the name and otherwise use the sa_data.
        if (it->first == otherName) {
          gotThem = true;
        }
        else if (res->ai_addrlen == it->second->ai_addrlen &&
                 memcmp(res->ai_addr, it->second->ai_addr, res->ai_addrlen) == 0)
        {
          gotThem = true;
        }
      }
      if (!gotThem) {
        printf("Welcome to the network: %s!\n", otherName.c_str());

        // Add to map
        them_addrss.emplace(make_pair(otherName, res));
        them_numbers.emplace(make_pair(otherName, atPlayerNumber));
        atPlayerNumber++;
        ////Send message of our new max connections
        if (host) {
          TotalConnections = int(them_addrss.size());
          const char *ConAmountMessage = "TotalConCount ";
          char ConAmountMessage2[50];
          int size = snprintf(ConAmountMessage2, 50, "%s%i", ConAmountMessage, TotalConnections);

          // Iterate over all the other peers
          for (it = them_addrss.begin(); it != them_addrss.end(); it++) {
            if (sendto(sockfd,
                       ConAmountMessage2,
                       size,
                       0,
                       it->second->ai_addr,
                       int(it->second->ai_addrlen)) == -1)
            {
              char errorBuffer[256];
              sprintf_s(errorBuffer,
                        sizeof(errorBuffer),
                        "Send failed: %s",
                        getSendError(WSAGetLastError()));
              //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
            }
          }
        }

        // Now return if we dont want to keep checking
        if (!keepChecking)
          return true;
      }

      // Hole Punching Check
      if (HolePunchingStatus == 1)  // listen() being called from holepunch()
      {
        // Now check if we already had this client in the hole punching vector
        gotThem = false;
        for (int i = 0; i < int(HolePunchConfirmedConnections.size()); i++) {
          if (res->ai_addrlen == HolePunchConfirmedConnections[i]->ai_addrlen &&
              memcmp(res->ai_addr, HolePunchConfirmedConnections[i]->ai_addr, res->ai_addrlen) ==
                  0)
          {
            gotThem = true;
          }
        }
        if (!gotThem) {
          TotalConnections++;
          HolePunchConfirmedConnections.push_back(res);
        }
      }
    }
  }
  return false;
}

// Using help from chatGPT
void Dance::sendCallBacks()
{
  QuitCallBack = false;
  while (!QuitCallBack) {
    while (!UserPackageStorage.empty()) {
      // Package name
      std::string_view packageName = getWord(UserPackageStorage.front(), 1);

      for (auto &callbackfunction : CallBackFunctions) {
        if (callbackfunction.first == packageName) {
          callbackfunction.second(UserPackageStorage.front());
        }
      }
      UserPackageStorage.pop();
    }
  }
}

void Dance::resetState()
{
  ipv = AF_INET;
  TotalConnections = 0;
  sockfd = 0;
  host = false;

  if (callBackThread.joinable()) {
    QuitCallBack = true;
    callBackThread.join();
  }
  HolePunchingStatus = 0;
  CurrentMoves = SAMEDEVICE;

  MaxConnections = 10;
  ReceivedMessages.clear();
  std::queue<std::string>().swap(UserPackageStorage);
  CallBackFunctions.clear();
  UseCallBackFunctions = false;
  forceIPV4 = false;

  them_addrss.clear();
  them_numbers.clear();
  HolePunchConfirmedConnections.clear();
}

void Dance::addImportantMessage(std::string &message, const int toUserID)
{
  importantMesStruct iMessage;
  // Create ID based on timeAlive and user ID in the front.
  float mult = std::pow(100.0f, std::floorf(std::log10(static_cast<float>(atImpMessage))));
  if (mult == 0)
    mult = 10.0f;
  iMessage.ID = static_cast<unsigned int>(toUserID * mult) + atImpMessage++;
  iMessage.message = message;
  iMessage.checksDone = 0;
  ImportantSendMessages.emplace(iMessage.ID, iMessage);
  message = "Imp" + std::to_string(iMessage.ID) + " " + message;
  iMessage;
}

bool Dance::handleDisconnection(sockaddr_storage input, int inputSize)
{
  // If we are not host, we just assume the peer has closed the connection, so we remove the client
  bool disconnected = false;
  // Check if we got this in our vector
  auto res = storageToAddrInfo(input, inputSize);

  std::map<std::string, struct addrinfo *>::iterator it;
  for (it = them_addrss.begin(); it != them_addrss.end(); it++) {
    if (memcmp(res->ai_addr, it->second->ai_addr, res->ai_addrlen) == 0) {
      // remove from vector and break
      printf("Peer %s has disconnected...\n", it->first.c_str());
      DisconnectedUserIDs.push_back(them_numbers.at(it->first));
      them_numbers.erase(it->first);
      them_addrss.erase(it->first);
      disconnected = true;

      // Update total amount of connections to all peers if host
      if (host) {
        TotalConnections = int(them_addrss.size());
        const char *ConAmountMessage = "TotalConCount ";
        char ConAmountMessage2[50];
        int size = snprintf(ConAmountMessage2, 50, "%s%i", ConAmountMessage, TotalConnections);

        // Iterate over all the other peers
        for (it = them_addrss.begin(); it != them_addrss.end(); it++) {
          if (sendto(sockfd,
                     ConAmountMessage2,
                     size,
                     0,
                     it->second->ai_addr,
                     int(it->second->ai_addrlen)) == -1)
          {
            char errorBuffer[256];
            sprintf_s(errorBuffer,
                      sizeof(errorBuffer),
                      "Send failed: %s",
                      getSendError(WSAGetLastError()));
            //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
          }
        }
      }
      else {
        // Reset state, since our host was our everything
        resetState();
      }
      return disconnected;
    }
  }
  return disconnected;
}

std::string_view Dance::getWord(const std::string &input, int wordNumber)
{
  if (&input == nullptr || input.empty()) {
    return {};
  }

  int EndOfWord = 0;
  int BeginOfWord = 0;

  // Get to the word
  int j = 0;
  for (int i = 0; i < wordNumber; i++) {
    while (input[j] != 32 && input[j] != '\0')
      j++;
    // Find next non-space
    while (input[j] == 32 && input[j] != '\0')
      j++;
  }
  // Get the word
  BeginOfWord = j;
  while (j < int(input.size()) && input[j] != 32 && input[j] != '\0')
    j++;
  EndOfWord = j;

  if (BeginOfWord != EndOfWord)
    return std::string_view(input.data() + BeginOfWord, EndOfWord - BeginOfWord);

  return {};
}

//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
//-----------------------------------------      Helper Functions
//-----------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------

std::string Dance::getIP(bool publicIP)
{
  if (publicIP) {
    std::string website_HTLM = get_Website();  // returns ipv4 or ipv6, whichever is possible
    if (website_HTLM != "0") {
      char OutputIP[65];
      strcpy_s(OutputIP, website_HTLM.c_str());  // Copy string to IP char
      return OutputIP;
      // Print
      // printf("Your IP adress = %s\n", website_HTLM.c_str());
    }
  }
  else {
    // Use the code from microsoft
    // https://learn.microsoft.com/nl-nl/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersinfo?redirectedfrom=MSDN
    PIP_ADAPTER_INFO pAdapterInfo;
    PIP_ADAPTER_INFO pAdapter = NULL;
    DWORD dwRetVal = 0;

    ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);
    pAdapterInfo = (IP_ADAPTER_INFO *)malloc(sizeof(IP_ADAPTER_INFO));
    if (pAdapterInfo == NULL) {
      //LOGLINE(DLogObj,
      //        DanceLogger::DANCE_ERROR,
      //       "Error allocating memory needed to call GetAdaptersinfo1");
    }
    // Make an initial call to GetAdaptersInfo to get
    // the necessary size into the ulOutBufLen variable
    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
      free(pAdapterInfo);
      pAdapterInfo = (IP_ADAPTER_INFO *)malloc(ulOutBufLen);
      if (pAdapterInfo == NULL) {
        //LOGLINE(DLogObj,
        //       DanceLogger::DANCE_ERROR,
        //        "Error allocating memory needed to call GetAdaptersinfo2");
      }
    }
    if ((dwRetVal = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen)) == NO_ERROR) {
      pAdapter = pAdapterInfo;
      while (pAdapter) {
        std::string IP = pAdapter->IpAddressList.IpAddress.String;

        // Check if IP = 0.0.0.0
        if (IP == "0.0.0.0") {
          pAdapter = pAdapter->Next;
          continue;
        }

        // Check for 192.168.x.x, 10.x.x.x, and 172.16.x.x - 172.31.x.x
        // unsigned int a, b;
        // sscanf_s(IP.c_str(), "%u.%u", &a, &b);  // CHATGPT suggested this
        // if ((a == 192 && b == 168) || (a == 10) || (a == 172 && (b >= 16 && b <= 31)))
        //{
        //}
        // else
        //{
        //    // COULD BREAK IF ONLY A PUBLIC IP EXIST.
        //    pAdapter = pAdapter->Next;
        //    continue;  // Not a private IP.
        //}

        // Check for virtual machine by checking if they have a gateway
        if (std::string(pAdapter->GatewayList.IpAddress.String) == "0.0.0.0") {
          pAdapter = pAdapter->Next;
          continue;  // Virtual Machine
        }

        // This is the IP we need
        char OutputIP[65];
        strcpy_s(OutputIP, IP.c_str());  // Copy string to IP char
        return OutputIP;

        // If we want to Continue:

        // Print IP Adress
        // printf("\tIP Address: \t%s\n", IP.c_str());
        // pAdapter = pAdapter->Next;
        // printf("\n");
      }
    }
    else {
      char errorBuffer[256];
      sprintf_s(
          errorBuffer, sizeof(errorBuffer), "GetAdaptersInfo failed with error: %d\n", dwRetVal);
      //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    }
    if (pAdapterInfo)
      free(pAdapterInfo);
  }
  return "0";
}

std::string Dance::get_Website()
{
  // Credits from https://stackoverflow.com/a/39567361

  WSADATA wsaData;
  SOCKET Socket;
  SOCKADDR_IN6 *SockAddr6;
  SOCKADDR_IN *SockAddr;
  std::string get_http;
  std::string Website_HTLM;
  char buffer[10000];
  std::string url = "api64.ipify.org";

  get_http = "GET / HTTP/1.1\r\nHost: " + url + "\r\nConnection: close\r\n\r\n";

  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, "WSASTARTUP failed trying to get the website");
    return "0";
  }

  struct addrinfo hints, *res, *p;
  int status;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  ipv = AF_INET6;

  // Get address info of the site, note that port 80 is used: http
  // Comment to force ipv4
  if (forceIPV4 || (status = getaddrinfo(url.c_str(), "80", &hints, &res)) != 0) {
    // Try it with IPV4
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    ipv = AF_INET;
    url = "api.ipify.org";
    if ((status = getaddrinfo(url.c_str(), "80", &hints, &res)) != 0) {
      char errorBuffer[256];
      sprintf_s(errorBuffer,
                sizeof(errorBuffer),
                "error while getaddrinfo ourself: %s",
                getSendError(WSAGetLastError()));
      //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
      return "0";
    }
  }

  for (p = res; p != NULL; p = p->ai_next) {
    // IPV6
    if (hints.ai_family == AF_INET6) {
      SockAddr6 = (SOCKADDR_IN6 *)p->ai_addr;
    }
    // IPV4
    else if (hints.ai_family == AF_INET) {
      SockAddr = (SOCKADDR_IN *)p->ai_addr;
    }
  }

  // Get socket
  Socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

  // And connect
  if (connect(Socket, res->ai_addr, int(res->ai_addrlen)) != 0) {
    char errorBuffer[256];
    sprintf_s(errorBuffer,
              sizeof(errorBuffer),
              "Could not connect: %s",
              getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    return "0";
  }

  if (send(Socket, get_http.c_str(), int(strlen(get_http.c_str())), 0) ==
      -1)  // Send request to site
  {
    char errorBuffer[256];
    sprintf_s(
        errorBuffer, sizeof(errorBuffer), "Could not send: %s", getSendError(WSAGetLastError()));
    //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
  }

  // Because we send a request to the site, it send some back to us with the information we need.
  // We unpack this information (very badly)
  int nDataLength;
  while ((nDataLength = recv(Socket, buffer, 10000, 0)) > 0) {
    int i = 0;
    while (buffer[i] >= 32 || buffer[i] == '\n' || buffer[i] == '\r') {
      // Checks for 2 new lines and a number after. Here is the IP
      if ((buffer[i] == '\n' || buffer[i] == '\r') &&
          (buffer[i + 1] == '\n' || buffer[i + 1] == '\r') && buffer[i + 2] >= 48 &&
          buffer[i + 2] <= 57)
      {
        i += 2;
        while (buffer[i] != '\n' && buffer[i] != '\r' && buffer[i] > 32) {
          Website_HTLM += buffer[i];
          i++;
        }
        break;
      }

      i += 1;
    }
  }

  freeaddrinfo(res);
  closesocket(Socket);
  WSACleanup();

  return Website_HTLM;
}

//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
//----------------------------------------     Messages and Packages
//--------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------

void Dance::sendMessageTo(std::string message, int themID, bool toHost, bool important)
{
  if (host) {
    // Iterate of numbers
    for (const auto &pair : them_addrss) {
      if (them_numbers.at(pair.first) == themID)  // Send to this one
      {
        if (important) {
          addImportantMessage(message, themID);
        }

        // Send the message
        if (sendto(sockfd,
                   message.c_str(),
                   int(message.size()),
                   0,
                   pair.second->ai_addr,
                   int(pair.second->ai_addrlen)) == -1)
        {
          char errorBuffer[256];
          sprintf_s(errorBuffer,
                    sizeof(errorBuffer),
                    "Send failed: %s",
                    getSendError(WSAGetLastError()));
          //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
        }
        // Go back
        return;
      }
    }
    // If here, themID is not available.
    //LOGLINE(DLogObj, DanceLogger::DANCE_WARNING, "them ID is not found");
    return;
  }
  else  // not host
  {
    if (toHost)  // Ignore themID, we send this one just to the host
    {
      std::string DataMessage = "-ToHo ";
      DataMessage += message;

      // Send the message to host
      if (sendto(sockfd,
                 DataMessage.c_str(),
                 int(DataMessage.size()),
                 0,
                 them_addrss.at("HOST")->ai_addr,
                 int(them_addrss.at("HOST")->ai_addrlen)) == -1)
      {
        char errorBuffer[256];
        sprintf_s(
            errorBuffer, sizeof(errorBuffer), "Send failed: %s", getSendError(WSAGetLastError()));
        //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
      }
    }
    else  // Send to the host which will send it to the right user
    {
      std::string DataMessage = "-ToCl-";
      DataMessage += std::to_string(themID) + " ";  // Add ID
      DataMessage += message;

      // Send the message to host
      if (sendto(sockfd,
                 DataMessage.c_str(),
                 int(DataMessage.size()),
                 0,
                 them_addrss.at("HOST")->ai_addr,
                 int(them_addrss.at("HOST")->ai_addrlen)) == -1)
      {
        char errorBuffer[256];
        sprintf_s(
            errorBuffer, sizeof(errorBuffer), "Send failed: %s", getSendError(WSAGetLastError()));
        //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
      }
    }
  }
}

bool Dance::isDisconnected(const int userID, bool remove)
{
  for (int i = 0; i < DisconnectedUserIDs.size(); i++) {
    if (userID == DisconnectedUserIDs.at(i)) {
      if (remove)
        DisconnectedUserIDs.erase(DisconnectedUserIDs.begin() + i);
      return true;
    }
  }
  return false;
}

void Dance::sendPackage(std::string PackageName, bool important)
{
  if (host) {
    char dataMessage[MAXPACKAGESIZE];

    preparePackage(PackageName.c_str(), dataMessage);

    // Iterate over all the other peers
    std::map<std::string, struct addrinfo *>::iterator it;
    for (it = them_addrss.begin(); it != them_addrss.end(); it++) {
      // TODO: possibly just use a char
      std::string dataMessageString = dataMessage;
      if (important)
        addImportantMessage(dataMessageString, them_numbers.at(it->first));
      dataMessageString += dataMessage;

      if (sendto(sockfd,
                 dataMessageString.c_str(),
                 int(strlen(dataMessageString.c_str())),
                 0,
                 it->second->ai_addr,
                 int(it->second->ai_addrlen)) == -1)
      {
        char errorBuffer[256];
        sprintf_s(
            errorBuffer, sizeof(errorBuffer), "Send failed: %s", getSendError(WSAGetLastError()));
        //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
      }
    }
  }
  else  // not host
  {
    // First send to host which will send to all //---------------------------------------------NOT
    // SURE IF THE TOALL WILL BE ADDED CORRECTLY-------------------------------------
    char dataMessage[MAXPACKAGESIZE] = "-ToAll ";
    preparePackage(PackageName.c_str(), dataMessage + strlen(dataMessage));

    if (them_addrss.size() > 0) {
      std::string dataMessageString;
      if (important)
        addImportantMessage(dataMessageString, them_numbers.at("HOST"));
      dataMessageString += dataMessage;

      // Send the message to host
      if (sendto(sockfd,
                 dataMessageString.c_str(),
                 int(strlen(dataMessageString.c_str())),
                 0,
                 them_addrss.at("HOST")->ai_addr,
                 int(them_addrss.at("HOST")->ai_addrlen)) == -1)
      {
        char errorBuffer[256];
        sprintf_s(
            errorBuffer, sizeof(errorBuffer), "Send failed: %s", getSendError(WSAGetLastError()));
        //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
      }
    }
  }
}

void Dance::sendPackageTo(std::string PackageName, int themID, bool ToHost, bool important)
{
  char DataMessage[MAXPACKAGESIZE];
  preparePackage(PackageName.c_str(), DataMessage);
  sendMessageTo(DataMessage, themID, ToHost, important);
}

void Dance::sendToSelf(std::string PackageName)
{
  MessageVectorMutex.lock();
  char DataMessage[MAXPACKAGESIZE];
  preparePackage(PackageName.c_str(), DataMessage);
  ReceivedMessages.insert(ReceivedMessages.begin(), DataMessage);
  MessageVectorMutex.unlock();
}

void Dance::preparePackage(const char *PackageName, char *DataMessage)
{
  // Get the data ready
  int offset = 0;
  memcpy(DataMessage + offset, name.c_str(), name.length());
  offset += int(name.length());
  std::memcpy(DataMessage + offset, ": ", 2);
  offset += 2;
  DataMessage[offset++] = ' ';
  memcpy(DataMessage + offset, PackageName, std::strlen(PackageName));
  offset += int(std::strlen(PackageName));
  DataMessage[offset++] = ' ';

  const auto &package = PackageMap[PackageName];
  const int size = int(package.size());

  // Loop over all the data and add it to the message
  for (int i = 0; i < size; i++) {
    // Get value
    const std::variant<int, float, unsigned, const char *, std::string, double, bool> &Value =
        package[i];

    // Cast from correct type to string
    if (auto ValueInt = std::get_if<int>(&Value)) {
      offset += snprintf(DataMessage + offset, MAXPACKAGESIZE - offset, "%i ", *ValueInt);
    }
    else if (auto ValueUnsigned = std::get_if<unsigned>(&Value)) {
      offset += snprintf(DataMessage + offset, MAXPACKAGESIZE - offset, "%i ", *ValueUnsigned);
    }
    else if (auto ValueDouble = std::get_if<double>(&Value)) {
      offset += snprintf(DataMessage + offset, MAXPACKAGESIZE - offset, "%f ", *ValueDouble);
    }
    else if (auto ValueFloat = std::get_if<float>(&Value)) {
      offset += snprintf(DataMessage + offset, MAXPACKAGESIZE - offset, "%f ", *ValueFloat);
    }
    else if (auto ValueString = std::get_if<std::string>(&Value)) {
      memcpy(DataMessage + offset, ValueString->c_str(), ValueString->length());
      offset += int(ValueString->length());
      DataMessage[offset++] = ' ';
    }
    else if (auto ValueChar = std::get_if<const char *>(&Value)) {
      memcpy(DataMessage + offset, ValueChar, std::strlen(*ValueChar));
      offset += int(std::strlen(*ValueChar));
      DataMessage[offset++] = ' ';
    }
    else if (auto ValueBool = std::get_if<bool>(&Value)) {
      offset += snprintf(
          DataMessage + offset, MAXPACKAGESIZE - offset, "%s ", *ValueBool ? "true" : "false");
    }
    // Message to big, report it and return
    if (offset > MAXPACKAGESIZE - 10) {
      //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, "Error, message size too big");
      return;
    }
  }
  offset += snprintf(DataMessage + offset, MAXPACKAGESIZE - offset, "%s", "end");

  return;
}

//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
//----------------------------------------    Other helper functions
//----------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------

addrinfo *Dance::storageToAddrInfo(sockaddr_storage input, int inputSize)
{
  addrinfo *res;  // Result

  // If we are on the same device, we dont want to store the others based on the IP.
  if (CurrentMoves == SAMEDEVICE) {
    // Malloc memory for res
    res = (addrinfo *)malloc(sizeof(addrinfo));

    if (res == NULL) {
      //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, "Malloc failed");
      return nullptr;
    }
    // Manually copy the adress
    res->ai_family = input.ss_family;
    res->ai_socktype = SOCK_DGRAM;

    res->ai_addr = (struct sockaddr *)malloc(inputSize);
    if (res->ai_addr == NULL) {
      //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, "Malloc failed");
      return nullptr;
    }
    memcpy(res->ai_addr, &input, inputSize);
    res->ai_addrlen = inputSize;
  }
  else {
    struct addrinfo hints;
    int status;
    char addr_str[INET6_ADDRSTRLEN] = {0};  // This will hold the IP

    if (input.ss_family == AF_INET)  // IPV4
    {
      // Get IP of other
      struct sockaddr_in *ipv4 = (struct sockaddr_in *)&input;
      inet_ntop(AF_INET, &ipv4->sin_addr, addr_str, sizeof(addr_str));
    }
    else  // IPV6
    {
      // Get IP of other
      struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&input;
      inet_ntop(AF_INET6, &ipv6->sin6_addr, addr_str, sizeof(addr_str));
    }

    // Handle this and put it inside the map
    memset(&hints, 0, sizeof hints);
    hints.ai_family = input.ss_family;
    hints.ai_socktype = SOCK_DGRAM;
    // addrinfo
    if ((status = getaddrinfo(addr_str, HOSTPORT, &hints, &res)) != 0) {
      char errorBuffer[256];
      sprintf_s(errorBuffer,
                sizeof(errorBuffer),
                "getaddrinfo error on clients IP: %s",
                getSendError(WSAGetLastError()));
      //LOGLINE(DLogObj, DanceLogger::DANCE_ERROR, errorBuffer);
    }
  }

  return res;
}

const char *Dance::getSendError(int errorCode)
{
  const char *output;

  switch (errorCode) {
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
               errorCode);
      output = buffer;
      break;
  }
  return output;
}
