#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <vector>
#include <map>
#include <poll.h>
#include <netinet/in.h>

#include "Command.hpp"
#include "Client.hpp"
#include "Channel.hpp"


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
    std::map<int, Client> _clients;
    std::map<std::string, Channel> _channels;

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

    bool isValidNickname(const std::string &nickname) const;
    bool isNicknameInUse(const std::string &nickname, int currentFd) const;
    bool isValidChannelName(const std::string &channelName) const;
    bool isChannelTarget(const std::string &channelName) const;

    Client *findClientByNickname(const std::string &nickname);
    Client *findClientByFd(int clientFd);
    std::string getReplyNickname(const Client &client) const;

    void sendServerReply(int clientFd,
                        const std::string &code,
                        const std::string &middle,
                        const std::string &message);

    bool requireRegistered(int clientFd, Client &client);

    void sendPrivateMessage(const Client &sender,
                            const Client &target,
                            const std::string &message);

    void handleCommand(int clientFd, const Command &cmd);
    void handlePing(int clientFd, const Command &cmd);
    void handlePrivmsg(int clientFd, const Command &cmd);
    void handlePass(int clientFd, const Command &cmd);
    void handleNick(int clientFd, const Command &cmd);
    void handleUser(int clientFd, const Command &cmd);
    void handleJoin(int clientFd, const Command &cmd);
    void handlePart(int clientFd, const Command &cmd);
    void handleKick(int clientFd, const Command &cmd);
    void handleInvite(int clientFd, const Command &cmd);
    void handleTopic(int clientFd, const Command &cmd);
    void handleMode(int clientFd, const Command &cmd);

    void tryRegisterClient(int clientFd);
    
    void handlePrivmsgToUser(const std::string &target, int clientFd, const std::string &senderNick, Client *sender, const std::string &message);
    void handlePrivmsgToChannel(const std::string &target, Client *sender, const std::string &message, int clientFd, const std::string &senderNick);


    void broadcastToChannel(Channel &channel, const std::string &message, Client *sender);

    void ensureChannelHasOperator(Channel &channel, const std::string &channelName);

    bool parseUserLimit(const std::string &value, size_t &limit) const;

    void appendAppliedMode(std::string &appliedModes,
                       char &lastOutputSign,
                       char sign,
                       char mode) const;

    void removeClientFromChannels(int clientFd);
    void cleanup();

public:
    Server(const char *portArg, const char *password);
    ~Server();

    void run();
};

#endif