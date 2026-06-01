#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <vector>
#include <map>
#include <poll.h>
#include <netinet/in.h>

#include "Command.hpp"

class Server
{
private:
    enum
    {
        BUFFER_SIZE = 512,
        WAIT_FOREVER = -1
    };

    int _port;
    std::string _password;
    int _serverFd;

    std::vector<pollfd> _pfds;
    std::map<int, std::string> _clientBuffers;

    Server(const Server &other);
    Server &operator=(const Server &other);

    int parsePort(const char *portArg);

    int createServerSocket();
    void enableReuseAddress();
    sockaddr_in createServerAddress();
    void bindServerSocket(sockaddr_in &addr);
    void startListening();

    void addFdToPoll(int fd);
    void acceptNewClient();
    void removeClient(size_t &i);
    void receiveFromClient(size_t &i);
    void extractCompleteLines(int clientFd, std::string &clientBuffer);

    void sendToClient(int clientFd, const std::string &message);

    void handleCommand(int clientFd, const Command &cmd);
    void handlePing(int clientFd, const Command &cmd);
    void handlePrivmsg(int clientFd, const Command &cmd);

    void cleanup();

public:
    Server(const char *portArg, const char *password);
    ~Server();

    void run();
};

#endif