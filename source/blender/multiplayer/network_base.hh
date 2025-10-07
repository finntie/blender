#pragma once

#pragma once
#define NOMINMAX /* Get rid of issues with MIN and MAX in winsock */
#include <winsock2.h>

#include <atomic>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "BLI_map.hh"
#include "BLI_vector.hh"

#define MAXPACKAGESIZE 1024

class Dance {

 public:
  /* Mode of connection. */
  enum dance_moves { SAMEDEVICE, LAN, PUBLIC };

 private:
  struct important_message_struct {
    std::string message{};
    uint32_t ID = 0;
    int checks_done = 0; /* How many checks it already did. */
  };

  /* IP information */
  int ipv_ = AF_INET; /* #AF_INET or #AF_INET6 for ipv4 or ipv6 */
  int total_connections_ = 0;

  /* Winsock Variables */
  SOCKET sockfd_{};

  /* Device Information */
  std::string name_ = "NONE";
  std::string device_name_ = "0";
  char host_port_[5] = "8392";
  bool is_host_ = false;
  int peer_ID = 0;                           /* Personal ID (0 == host). */
  std::atomic<bool> quit_listening_ = false; /* Will only be set by the main thread exiting */
  std::atomic<bool> quit_callback_ = false;  /* Will only be set by the main thread exiting */
  int hole_punching_status_ = 0;
  dance_moves current_moves_ = SAMEDEVICE;

  /* Other important information */
  float keep_alive_time_ = 60.0f;
  float quick_keep_alive_time_ = 0.5f;
  int max_connections_ = 10; /* Standard is 10 */
  std::deque<std::string> received_messages_{};
  blender::Map<uint32_t, important_message_struct> important_send_messages_{};
  std::queue<std::string> user_package_storage_{};
  blender::Map<std::string, std::function<void(const std::string &)>> callback_functions_;
  std::future<bool> connection_made_;
  std::thread listen_thread_;
  std::thread callback_thread_;

  bool use_callback_functions_ = false;
  bool force_IPV4_ = false;
  blender::Vector<int> disconnected_user_IDs_{};
  int at_player_number_ = 1;
  uint32_t at_imp_message_ = 0;
  uint64_t last_time_ = 0;
  float time_checked_imp_ = 0.0f;

  /* thread safety */
  std::mutex message_vector_mutex_;
  std::mutex callback_mutex_;

  /* Map of Data Lists, which holds the data. Data types can be expanded. */
  blender::Map<
      std::string,
      blender::Vector<std::variant<int, float, uint32_t, const char *, std::string, double, bool>>>
      package_map_{};

  /* Map of others, key = name */
  blender::Map<std::string, struct addrinfo *> them_addrss_{};
  blender::Map<std::string, int> them_numbers_{};
  /* Vector of connections that are confirmed. */
  blender::Vector<struct addrinfo *> hole_punch_confirmed_connections_{};

 public:
  Dance() {};

  ~Dance();

  /**
   * Initializes the networking, always call this first.
   *
   * \param use_callback: Do you want to use callback functions?
   * \param force_IPV4: Do you want to force IPV4 or possibly use IPV6?
   */
  void MU_init(bool use_callback, bool force_IPV4);

  /* -------------------------------------------------------------------- */
  /** \Engine Functions
   *
   * Most common and important functions.
   * \{ */

  /**
   * Host a lobby for other clients to join.
   *
   * \param moves: Are the programs on the same device, is it LAN (Same network different device),
   * or is it public (Other network).
   * \param max_connections_count: Maximum amount of clients able to connect to this lobby.
   * \param port: Port number, (leaving it at 0 will cause it grabbing the default port number).
   * \param force_IPV4: Always use IPV4, else the program will try IPV6 if possible
   * (This will cause connection issues if other player does not match this IPV).
   * \param clients_IP: If chosen for public network, the host has to fill in all the clients IPs.
   */
  void MU_host(dance_moves moves,
               int max_connections_count,
               const char *port = 0,
               bool force_IPV4 = false,
               const blender::Vector<std::string> &clients_IP = blender::Vector<std::string>());

  /**
   * Connect to a server.
   *
   * \param moves: Are the programs on the same device, is it LAN (Same network different device),
   * or is it public (Other network).
   * \param host_IP: The IP of the host (server). This does not need to be filled in for same
   * device connection.
   * \param port: Port number, (leaving it at 0 will cause it grabbing the default port number).
   * \param force_IPV4: Always use IPV4, else the program will try IPV6 if possible
   * (This will cause connection issues if other player does not match this IPV).
   * \return if connection succeeded.
   */
  bool MU_connect(dance_moves moves,
                  const char *host_IP,
                  const char *port = 0,
                  bool force_IPV4 = false);

  /**
   * Call this every tick to ensure connections will be stayed alive, especially if using important
   * messages or hole punching.
   */
  void MU_keep_alive(float deltatime);

  /**
   * Retrieve the IP of this device.
   *
   * \param public_IP: Is the public or private IP wanted?
   * \return The IP address.
   */
  std::string MU_get_IP(bool public_IP);

  /**
   * Sent a message to specified ID or to the host
   *
   * \param message: Message to be send.
   * \param them_ID: ID to send it to. '0 = host'.
   * \param only_to_host: Send the message to the host? (ignores them_ID).
   * \param important: Treat the message with more care?
   */
  void MU_send_message_to(std::string message, int them_ID, bool only_to_host, bool important);

  /**
   * Get total amount of connections. This does not include ourself.
   */
  int MU_get_total_connections()
  {
    return total_connections_;
  }

  /**
   * Are we the host?
   */
  bool MU_get_if_host()
  {
    return is_host_;
  }

   /**
   * What is our number?
   */
  int MU_get_player_number()
  {
    return peer_ID;
  }

  /**
   * Is IPV4 used? (else, IPV6 is used).
   */
  bool MU_get_use_IPV4()
  {
    return (ipv_ == 2);
  }

  /**
   * Sets to forcing of ipv4, if you for example, want to get the public IPV4 or IPV6 before
   * connecting.
   */
  void MU_set_force_IPV4(bool _force_IPV4)
  {
    force_IPV4_ = _force_IPV4;
  };

  /**
   * Check if the holepunching succeeded.
   *
   * \return 0 if not holepunching at all, 1 if holepunching at the moment, 2 if holepunching
   * succeeded.
   */
  int MU_hole_punch_succeeded()
  {
    return hole_punching_status_;
  }

  /**
   * Check if a certain player has disconnected
   *
   * \param user_ID: Number of the peer.
   * \param remove: Remove value from vector of names.
   * \return True if vector of disconnected users contains this name.
   */
  bool MU_is_disconnected(const int user_ID, bool remove);

  /** \} */

  /* -------------------------------------------------------------------- */
  /** \Package Functions
   *
   * Functions related to receiving or sending packages
   * \{ */

  /**
   * Create a package, you may already fill it with some variables
   *
   * \param package_name: Name of the package.
   * \param args: Variables you want to add, such as 1.0f, true, 5, etc.
   */
  template<typename... Args> void MU_create_package(std::string package_name, Args... args)
  {
    /* Making a vector with all the parameters the user has put in.
     */
    blender::Vector<std::variant<int, float, uint32_t, const char *, std::string, double, bool>>
        input_vector{args...};
    package_map_.add(package_name, input_vector);
  }

  /**
   * Adds variables to this package.
   *
   * \param package_name: Name of the package.
   * \param args: Variables you want to add, such as 1.0f, true, 5, etc.
   */
  template<typename... Args>
  void MU_add_parameters_to_package(std::string package_name, Args... args)
  {
    package_map_.lookup(package_name).insert(package_map_.lookup(package_name).end(), {args...});
  }

  /**
   * Change the data of the selected variable.
   *
   * \param package_name: Name of the package.
   * \param variable_index: Index of the variable you want to change, in order of when variables
   * were added.
   * \param Data: Variable data.
   */
  template<typename T>
  void MU_add_data_to_parameter(std::string package_name, int variable_index, T Data)
  {
    package_map_.lookup(package_name)[variable_index] = Data;
  }

  /**
   * Deletes the variable from the package
   *
   * \param package_name: Name of the package.
   * \param number: Index of the variable you want to remove.
   * \param all: Ignores the number and just deletes all variables in this package.
   */
  void MU_delete_parameter_from_package(std::string package_name, int number, bool all)
  {
    if (all) {
      package_map_.lookup(package_name).clear();
    }
    else {
      package_map_.lookup(package_name).remove(number);
    }
  }

  /**
   * Create a callback function for a certain package. This function will be called when this
   * package arrives.
   *
   * \param package_name: Name of the package you want to receive a call for.
   * \param function: Function that you created that will handle this package.
   */
  void MU_create_package_callback_function(std::string package_name,
                                           std::function<void(const std::string &data)> function)
  {
    callback_functions_.add(package_name, function);
  }

  /**
   * Send the created package to all connections
   *
   * \param package_name: Name of the package you want to receive a call for.
   * \param important: Treat the message with more care?
   */
  void MU_send_package(std::string package_name, bool important = false);

  /**
   * Send a package to an individual user based on order of connections.
   * If we are not the host, this will first send it to the host which then will decide where it
   * will go (internally).
   *
   * \param package_name: Name of the package you want to receive a call for.
   * \param them_ID: ID of the client, this order is defined by the host (0 == host).
   * \param only_to_host: Only send package to host.
   * \param important: Treat the message with more care?
   */
  void MU_send_package_to(std::string package_name,
                          int them_ID,
                          bool only_to_host,
                          bool important = false);

  /**
   * Send a package to ourself (Add it straight to the incomming messages)
   *
   * \param package_name: Name of the package you want to receive a call for.
   */
  void MU_send_to_self(std::string package_name);

  /**
   * Prepares the package to a 'char*'.
   *
   * \param package_name: Name of the package you want to receive a call for.
   * \param r_buffer: Storage for the output.
   */
  void MU_prepare_package(const char *package_name, char *r_buffer);

  struct return_package_info {
    bool succeeded = false;
    blender::Vector<std::variant<int, float, uint32_t, const char *, std::string, double, bool>>
        variable_vector;
    template<typename T> T get_variable(int Index)
    {
      if (auto *value = std::get_if<T>(&variable_vector[Index])) {
        return *value;
      }
      else {
        /* TODO: how would the user know at run time to change get type? */
        printf("Error get_variable(): Wrong type\n");
        return T{};
      }
    }
    /* TODO: Add timestamp. */
    /* std::string TimeStamp; */
  };

  /* ChatGPT recommendation */
  /**
   * Try to parse a type to check if they compare.
   */
  template<typename T> bool MU_try_parse(std::string &input, T &r_output)
  {
    std::stringstream ss_temp(input);
    ss_temp >> std::boolalpha >>
        r_output; /* Use std::boolalpha to handle "true"/"false" strings for bool. */
    return !ss_temp.fail() && ss_temp.eof(); /* Check if succeeded */
  }

  /**
   * Get package that was received.
   *
   * \param package_name: Name of the package you want to receive a call for.
   * \param delete_message: Delete the package after it has been read?
   * \param r_package_info: Struct that will fill with a bool that states if the action was
   * successfull + a vector of all included types.
   */
  void MU_get_package(
      std::string package_name,
      bool delete_message,
      return_package_info *r_package_info); /* TODO : can be placed inside the cpp file */

  template<typename T> static T MU_data_to_variable(const std::string &data, int variable_index)
  {
    std::string tmp{};
    int word_index = 0;
    while (!(tmp = get_word(data, word_index++)).empty()) {
      /* This is the variable we are looking for */
      if (variable_index == word_index - 1) {
        std::stringstream ss_temp(tmp);
        T value{};
        ss_temp >> value; /* Put the string into the value which will be converted. */
        return value;
      }
    }
    return T{};
  }

  /**
   * Get a word out of a string, returns empty when invalid.
   *
   * \param input: Input string.
   * \param word_number: Word you want to get, starts at 0
   * \return A string view of the word.
   */
  static std::string_view get_word(const std::string &input, int word_number);

  /** \} */

 private:
  /**
   * Listens for incomming messages, acts uppon what to do with them.
   * Thread function that will repeat forever until told to stop.
   *
   * \param keep_checking: Keep checking even after we found a connection?
   * \return If we made a connection.
   */
  bool listen(bool keep_checking);

  /**
   * Keeps sending messages to all connections until connection is made
   */
  void hole_punch();

  /**
   * Sends the package to all the callbacks as thread
   */
  void send_callbacks();

  /**
   * Resets connections and variables, does NOT reset the map of the set packages.
   */
  void reset_state();

  /* -------------------------------------------------------------------- */
  /** \Helper Functions
   *
   * Functions that help other functions.
   * \{ */

  /**
   * Add the important message to the queue
   *
   * \param message: The important message.
   * \param to_user_ID: The user it is going to be send to.
   */
  void add_important_message(std::string &message, const int to_user_ID);

  /**
   * Handle a disconnection from a client or the host
   *
   * \param input: Peer info from the disconnected client.
   * \param input_size: Size of the input sockaddr_storage.
   * \return If we disconnected the user.
   */
  bool handle_disconnection(sockaddr_storage input, int input_size);

  /**
   * Converts #sockaddr_storage to #addrinfo*.
   *
   * \param input: Information of a user you want to convert.
   * \param input_size: Size of the input.
   * \return The information but now as #addrinfo*.
   */
  addrinfo *storage_to_addr_info(sockaddr_storage input, int input_size);

  /**
   * Returns your public IP
   * Credit: https:/*stackoverflow.com/a/39567361
   */
  std::string get_website();

  /**
   * Convert error code to string
   *
   * \param error_code: Error code retrieved from #WSAGetLastError().
   * \return A string view of the word.
   */
  static const char *get_send_errors(int error_code);

  /** \} */
};
