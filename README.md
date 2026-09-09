*This project has been created as part of the 42 curriculum by okhourss , sdriouec , iel-ouar .*

# ft_irc

## Description

`ft_irc` is a custom Internet Relay Chat (IRC) server written in **C++98**.

The goal of this project is to implement a functional IRC server capable of handling multiple clients simultaneously through **TCP/IP connections**, without using forking. The server uses **non-blocking sockets** and a single `poll()` loop to monitor the listening socket and all connected clients.

The server allows IRC clients to:

* Connect using a server password.
* Register with a nickname and username.
* Join and leave channels.
* Send private messages to other users.
* Send messages to channels.
* Receive messages from other channel members.
* Use channel operator privileges.
* Manage channel modes and permissions.

The project implements the mandatory requirements of the `ft_irc` subject. It does **not** implement an IRC client or server-to-server communication.

## Features

### Client Registration

The server supports the basic registration process:

* `PASS` — Authenticate using the server password.
* `NICK` — Set or change a nickname.
* `USER` — Set the username and complete registration.

A client must successfully authenticate and register before using commands that require a registered user.

### Channel Management

The server supports:

* `JOIN` — Join or create a channel.
* `PART` — Leave a channel.
* `PRIVMSG` — Send messages to a channel or another user.
* Channel membership management.
* Channel topics.
* Channel operators.

Messages sent to a channel are forwarded to the other clients currently joined to that channel.

### Channel Operator Commands

Channel operators can use:

* `KICK` — Remove a client from a channel.
* `INVITE` — Invite a client to a channel.
* `TOPIC` — View or change the channel topic.
* `MODE` — Manage channel modes.

The following channel modes are implemented:

| Mode | Description                                 |
| ---- | ------------------------------------------- |
| `i`  | Enable or disable invite-only mode          |
| `t`  | Restrict topic changes to channel operators |
| `k`  | Set or remove the channel password          |
| `o`  | Give or remove channel operator privileges  |
| `l`  | Set or remove the channel user limit        |

### Other Commands

The server also handles:

* `PING` — Respond with `PONG`.
* Unknown commands — Return an IRC error response.
* IRC messages using `\r\n` line termination.

## Technical Architecture

The project is divided into four main components:

### `Server`

The `Server` class is responsible for:

* Creating and configuring the listening socket.
* Binding and listening on the configured port.
* Accepting new clients.
* Setting sockets to non-blocking mode.
* Managing the `poll()` file descriptor list.
* Receiving client data.
* Sending pending data.
* Dispatching IRC commands.
* Managing connected clients and channels.
* Cleaning up sockets and resources.

The server uses one `poll()` loop to monitor the listening socket and all connected clients.

```text
                    +------------------+
                    |   IRC Client(s)  |
                    +--------+---------+
                             |
                          TCP/IP
                             |
                             v
                    +------------------+
                    |      Server      |
                    |                  |
                    |  Non-blocking    |
                    |     sockets      |
                    |                  |
                    |      poll()      |
                    +--------+---------+
                             |
              +--------------+--------------+
              |              |              |
              v              v              v
          Client(s)       Channel(s)     Commands
```

### `Client`

The `Client` class stores the state of an individual connected client, including:

* Socket file descriptor.
* Receive buffer.
* Send buffer.
* Nickname.
* Username.
* Password authentication state.
* Registration state.

Separate receive and send buffers allow the server to correctly handle partial network transfers and non-blocking output.

### `Channel`

The `Channel` class manages:

* Channel name.
* Connected clients.
* Channel operators.
* Invited clients.
* Channel topic.
* Invite-only mode.
* Topic restrictions.
* Channel password/key.
* User limit.

### `Command`

The `Command` structure represents a parsed IRC command.

The command parser:

1. Extracts the command name.
2. Converts the command to uppercase.
3. Separates its parameters.
4. Handles the IRC trailing parameter beginning with `:`.

The server then dispatches the parsed command to the appropriate handler.

## Non-Blocking I/O

All client sockets are configured in non-blocking mode.

The server uses a single `poll()` call to monitor:

* The listening socket.
* Incoming client data.
* Writable client sockets when there is pending data.

This prevents one slow client from blocking communication with the other clients.

The server also maintains buffers for both receiving and sending data.

For received data, complete IRC commands are extracted only when a newline is available. This allows the server to correctly handle commands that arrive in multiple TCP packets.

For outgoing data, messages are stored in a send buffer and transmitted when `poll()` reports that the socket is writable.

## Requirements

The project follows the mandatory requirements of the `ft_irc` subject:

* C++98.
* Compiled with `-Wall -Wextra -Werror`.
* TCP/IP communication.
* Non-blocking I/O.
* Multiple simultaneous clients.
* No forking.
* One `poll()` loop for I/O multiplexing.
* IRC client compatibility.
* Client authentication.
* Nickname and username registration.
* Channels and channel messaging.
* Private messaging.
* Channel operators.
* `KICK`.
* `INVITE`.
* `TOPIC`.
* `MODE` with `i`, `t`, `k`, `o`, and `l`.

The subject allows `poll()` or an equivalent mechanism such as `select()`, `kqueue()`, or `epoll()`. This implementation uses `poll()`.

## Instructions

### Compilation

The project includes a `Makefile`.

Compile the server with:

```bash
make
```

This creates the executable:

```text
ircserv
```

The available Makefile rules are:

```bash
make
make clean
make fclean
make re
```

* `make` — Compile the project.
* `make clean` — Remove object files.
* `make fclean` — Remove object files and the executable.
* `make re` — Clean everything and rebuild the project.

### Execution

The server requires two arguments:

```bash
./ircserv <port> <password>
```

For example:

```bash
./ircserv 6667 mysecretpassword
```

The `port` specifies the TCP port on which the server listens.

The `password` is required by clients when connecting to the server.

This follows the execution format required by the subject.

## Testing

### Using an IRC Client

Connect to the server using an IRC client of your choice.

Configure the client to connect to:

```text
Server: 127.0.0.1
Port: 6667
Password: mysecretpassword
```

After connecting, register with your nickname and username, then test commands such as:

```text
PASS mysecretpassword
NICK alice
USER alice 0 * :Alice
JOIN #42
PRIVMSG #42 :Hello everyone!
```

You can then test channel operator commands such as:

```text
TOPIC #42 :Welcome to the channel
MODE #42 +i
MODE #42 +t
MODE #42 +k secret
MODE #42 +l 10
KICK #42 bob
INVITE bob #42
```

### Testing with Netcat

The subject provides `nc` as a simple way to test the server and, in particular, partial TCP data.

Start the server:

```bash
./ircserv 6667 mysecretpassword
```

Then connect from another terminal:

```bash
nc -C 127.0.0.1 6667
```

The server must correctly handle commands that are received in multiple parts. TCP does not guarantee that one `send()` from a client corresponds to one `recv()` on the server, so the server aggregates received data in a buffer before processing complete commands.

For example, the subject demonstrates sending:

```text
com
man
d\n
```

which must be reconstructed as one complete command before processing.

## Project Structure

```text
ft_irc/
├── Makefile
├── include/
│   ├── Channel.hpp
│   ├── Client.hpp
│   ├── Command.hpp
│   └── Server.hpp
│
└── src/
    ├── Channel.cpp
    ├── Client.cpp
    ├── Command.cpp
    ├── Server.cpp
    └── main.cpp
```

### Source Files

| File          | Purpose                                                      |
| ------------- | ------------------------------------------------------------ |
| `main.cpp`    | Validates arguments and starts the server                    |
| `Server.cpp`  | Server socket, `poll()`, clients, commands and communication |
| `Client.cpp`  | Client state and communication buffers                       |
| `Channel.cpp` | Channel members, operators, modes and topic                  |
| `Command.cpp` | IRC command parsing                                          |
| `*.hpp`       | Class and structure declarations                             |

## Resources

### IRC Protocol

* [RFC 1459 — Internet Relay Chat Protocol](https://datatracker.ietf.org/doc/html/rfc1459)
* [RFC 2812 — Internet Relay Chat: Client Protocol](https://datatracker.ietf.org/doc/html/rfc2812)

These RFCs were used as references for understanding IRC commands, message formats, channels, users and server/client communication.

### Network Programming

* Beej's Guide to Network Programming — socket programming and network communication.
* Linux/POSIX documentation for:

  * `socket()`
  * `bind()`
  * `listen()`
  * `accept()`
  * `send()`
  * `recv()`
  * `fcntl()`
  * `poll()`

These resources were used to understand TCP sockets, non-blocking file descriptors and I/O multiplexing.

### C++98

The project follows the C++98 standard as required by the subject.

The main C++ concepts used include:

* Classes and encapsulation.
* Constructors and destructors.
* References.
* Standard containers such as `std::map`, `std::vector` and `std::string`.
* Exceptions.
* Object composition.

## AI Usage Disclosure

AI tools were used as a learning and development support tool during the project.

AI assistance was used for:

* **Networking concepts:** Understanding TCP sockets, non-blocking I/O, `poll()`, `recv()`, `send()`, and how clients interact with the server.
* **Architecture:** Discussing how to separate responsibilities between `Server`, `Client`, `Channel`, and `Command`.
* **Parsing:** Understanding how to process IRC commands and handle data received in partial TCP packets.
* **Debugging:** Investigating errors and unexpected server behavior during development.
* **Testing:** Discussing ways to test multiple clients, channel communication, commands, and partial data.
* **Documentation:** Helping organize and improve the project documentation.

All AI-generated explanations and suggestions were reviewed, tested, and adapted to the project. The goal was to understand the implementation rather than blindly copy generated code.

The project subject specifically states that AI-generated content should only be used when the student fully understands and can take responsibility for it.

## Limitations

This project implements the mandatory part of `ft_irc`.

The following are intentionally outside the scope of the mandatory implementation:

* IRC client development.
* Server-to-server communication.
* Optional bonus features such as file transfer and bots.

The subject specifies file transfer and bots as bonus features, which are evaluated only when the mandatory part is completely functional.
