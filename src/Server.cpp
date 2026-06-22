#include "../include/Server.hpp"

#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>

Server::Server(const char *portArg, const char *password)
    : _port(0),
      _password(password),
      _serverFd(-1)
{
    try
    {
        _port = parsePort(portArg);

        _serverFd = createServerSocket();

        enableReuseAddress();

        sockaddr_in addr = createServerAddress();

        bindServerSocket(addr);

        startListening();

        addFdToPoll(_serverFd);
    }
    catch (...)
    {
        cleanup();
        throw;
    }
}

Server::~Server()
{
    cleanup();
}

int Server::parsePort(const char *portArg)
{
    if (portArg == NULL || portArg[0] == '\0')
        throw std::runtime_error("invalid port: empty value");

    for (size_t i = 0; portArg[i] != '\0'; i++)
    {
        if (portArg[i] < '0' || portArg[i] > '9')
            throw std::runtime_error("invalid port: must contain only digits");
    }

    errno = 0;
    long port = std::strtol(portArg, NULL, 10);

    if (errno == ERANGE)
        throw std::runtime_error("invalid port: number is too large");

    if (port < 1 || port > 65535)
        throw std::runtime_error("invalid port: must be between 1 and 65535");

    return static_cast<int>(port);
}

int Server::createServerSocket()
{
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);

    if (serverFd < 0)
        throw std::runtime_error("socket failed");

    return serverFd;
}

void Server::enableReuseAddress()
{
    int opt = 1;

    if (setsockopt(_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        throw std::runtime_error("setsockopt failed");
}

sockaddr_in Server::createServerAddress()
{
    sockaddr_in addr;

    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    return addr;
}

void Server::bindServerSocket(sockaddr_in &addr)
{
    if (bind(_serverFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "bind failed: " << std::strerror(errno) << std::endl;
        throw std::runtime_error("bind failed");
    }
}

void Server::startListening()
{
    if (listen(_serverFd, SOMAXCONN) < 0)
        throw std::runtime_error("listen failed");

    std::cout << "Server is listening on port " << _port << "..." << std::endl;
    std::cout << "Waiting for clients..." << std::endl;
}

void Server::addFdToPoll(int fd)
{
    pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    _pfds.push_back(pfd);
}

void Server::acceptNewClient()
{
    int clientFd = accept(_serverFd, NULL, NULL);

    if (clientFd < 0)
        throw std::runtime_error("accept failed");

    addFdToPoll(clientFd);
    _clients[clientFd] = Client(clientFd);
    std::cout << "New client connected, fd = " << clientFd << std::endl;
}

void Server::removeClient(size_t &i)
{
    int clientFd = _pfds[i].fd;

    std::cout << "Client disconnected, fd = " << clientFd << std::endl;

    removeClientFromChannels(clientFd);

    close(clientFd);
    _clients.erase(clientFd);
    _pfds.erase(_pfds.begin() + i);

    if (i > 0)
        i--;
}

void Server::receiveFromClient(size_t &i)
{
    int clientFd = _pfds[i].fd;
    char buffer[BUFFER_SIZE];

    int bytes = recv(clientFd, buffer, sizeof(buffer), 0);

    if (bytes < 0)
    {
        std::cerr << "recv failed for fd " << clientFd << std::endl;
        removeClient(i);
        return;
    }

    if (bytes == 0)
    {
        removeClient(i);
        return;
    }

    Client *client = findClientByFd(clientFd);

    if (client == NULL)
        return;

    std::string received(buffer, bytes);

    client->getBuffer() += received;

    std::cout << "Received chunk from fd "
              << clientFd << ": [" << received << "]" << std::endl;

    extractCompleteLines(clientFd, client->getBuffer());
}

void Server::extractCompleteLines(int clientFd, std::string &clientBuffer)
{
    size_t pos;

    while ((pos = clientBuffer.find('\n')) != std::string::npos)
    {
        std::string line = clientBuffer.substr(0, pos);

        clientBuffer.erase(0, pos + 1);

        if (!line.empty() && line[line.length() - 1] == '\r')
            line.erase(line.length() - 1);

        std::cout << "Complete line from fd "
                  << clientFd << ": [" << line << "]" << std::endl;

        Command cmd = parseCommand(line);

        handleCommand(clientFd, cmd);
    }
}

void Server::sendToClient(int clientFd, const std::string &message)
{
    if (send(clientFd, message.c_str(), message.length(), 0) < 0)
        std::cerr << "send failed for fd " << clientFd << std::endl;
}

void Server::handleCommand(int clientFd, const Command &cmd)
{
    if (cmd.cmd.empty())
        return;

    if (cmd.cmd == "PING")
        handlePing(clientFd, cmd);
    else if (cmd.cmd == "PRIVMSG")
        handlePrivmsg(clientFd, cmd);
    else if (cmd.cmd == "PASS")
        handlePass(clientFd, cmd);
    else if (cmd.cmd == "NICK")
        handleNick(clientFd, cmd);
    else if (cmd.cmd == "USER")
        handleUser(clientFd, cmd);
    else if (cmd.cmd == "JOIN")
        handleJoin(clientFd, cmd);
    else if (cmd.cmd == "PART")
        handlePart(clientFd, cmd);
    else
        std::cout << "Unknown command: [" << cmd.cmd << "]" << std::endl;
}

void Server::handlePing(int clientFd, const Command &cmd)
{
    if (cmd.params.empty())
    {
        sendToClient(clientFd, "PONG\r\n");
        return;
    }

    sendToClient(clientFd, "PONG " + cmd.params[0] + "\r\n");
}

void Server::handlePrivmsg(int clientFd, const Command &cmd)
{
    Client *sender = findClientByFd(clientFd);

    if (sender == NULL)
        return;

    if (!requireRegistered(clientFd, *sender))
        return;

    std::string senderNick = getReplyNickname(*sender);

    if (cmd.params.empty())
    {
        sendServerReply(clientFd,
                        "411",
                        senderNick,
                        "No recipient given (PRIVMSG)");
        return;
    }

    if (cmd.params.size() < 2 || cmd.params[1].empty())
    {
        sendServerReply(clientFd,
                        "412",
                        senderNick,
                        "No text to send");
        return;
    }

    std::string target = cmd.params[0];

    bool isTargetChannel = isChannelTarget(target);

    if (isTargetChannel && !isValidChannelName(target))
    {
        sendServerReply(clientFd,
                        "403",
                        senderNick + " " + target,
                        "No such channel");
        return;
    }

    std::string message = cmd.params[1];

    if (isTargetChannel)
        handlePrivmsgToChannel(target, sender, message, clientFd, senderNick);
    else
        handlePrivmsgToUser(target, clientFd, senderNick, sender, message);
}

void Server::handlePrivmsgToChannel(const std::string &target,
                                    Client *sender,
                                    const std::string &message,
                                    int clientFd,
                                    const std::string &senderNick)
{
    std::map<std::string, Channel>::iterator it  = _channels.find(target);
    
    if (it == _channels.end())
    {
        sendServerReply(clientFd,
                        "401",
                        senderNick + " " + target,
                        "No such nick/channel");
        return;
    }

    if (!it->second.hasClient(clientFd))
    {
        sendServerReply(clientFd,
                        "442",
                        senderNick + " " + target,
                        "You're not on that channel");
        return;
    }
    std::string channelMessage = ":" + sender->getNickname() + " PRIVMSG " + it->second.getName() + " :" + message + "\r\n";

    broadcastToChannel(it->second, channelMessage, sender);
}

void Server::handlePrivmsgToUser(const std::string &target, int clientFd, const std::string &senderNick, Client *sender, const std::string &message)
{
    Client *targetClient = findClientByNickname(target);

    if (targetClient == NULL || !targetClient->isRegistered())
    {
        sendServerReply(clientFd,
                        "401",
                        senderNick + " " + target,
                        "No such nick/channel");
        return;
    }

    sendPrivateMessage(*sender, *targetClient, message);
}

void Server::handlePass(int clientFd, const Command &cmd)
{
    Client *client = findClientByFd(clientFd);

    if (client == NULL)
        return;

    if (cmd.params.empty())
    {
        sendServerReply(clientFd,
                "461",
                getReplyNickname(*client) + " PASS",
                "Not enough parameters");
        return;
    }

    if (client->isRegistered())
    {
        sendServerReply(clientFd,
                        "462",
                        getReplyNickname(*client),
                        "You may not reregister");
        return;
    }

    if (cmd.params[0] != _password)
    {
        sendServerReply(clientFd, "464", "*", "Password incorrect");
        return;
    }

    client->setPassAccepted(true);

    std::cout << "PASS accepted for fd " << clientFd << std::endl;

    tryRegisterClient(clientFd);
}

void Server::run()
{
    while (true)
    {
        int ret = poll(&_pfds[0], _pfds.size(), WAIT_FOREVER);

        if (ret < 0)
            throw std::runtime_error("poll failed");

        for (size_t i = 0; i < _pfds.size(); i++)
        {
            if (_pfds[i].revents & POLLIN)
            {
                if (_pfds[i].fd == _serverFd)
                    acceptNewClient();
                else
                    receiveFromClient(i);
            }
        }
    }
}

void Server::cleanup()
{
    bool serverFdClosed = false;

    for (size_t i = 0; i < _pfds.size(); i++)
    {
        if (_pfds[i].fd == _serverFd)
            serverFdClosed = true;

        close(_pfds[i].fd);
    }

    if (!serverFdClosed && _serverFd != -1)
        close(_serverFd);

    _pfds.clear();
    _channels.clear();
    _clients.clear();
    _serverFd = -1;
}

bool Server::isValidNickname(const std::string &nickname) const
{
    if (nickname.empty())
        return (false);

    if (nickname[0] >= '0' && nickname[0] <= '9')
        return (false);
    
    for (size_t i = 0; i < nickname.length(); i++)
    {
        char c = nickname[i];

        if (c == ' ' || c == ',' || c == '*' || c == '?' ||
            c == '!' || c == '@' || c == '.' || c == ':')
            return (false);
    }

    return (true);
}

bool Server::isNicknameInUse(const std::string &nickname, int currentFd) const
{
    std::map<int, Client>::const_iterator it;

    for (it = _clients.begin(); it != _clients.end(); ++it)
    {
        if (it->first != currentFd &&
            toUpper(it->second.getNickname()) == toUpper(nickname))
        return (true);
    }

    return (false);
}

void Server::handleNick(int clientFd, const Command &cmd)
{
    Client *client = findClientByFd(clientFd);

    if (client == NULL)
        return;

    std::string replyNick = getReplyNickname(*client);

    if (cmd.params.empty())
    {
        sendServerReply(clientFd,
                        "431",
                        replyNick,
                        "No nickname given");
        return;
    }

    std::string nickname = cmd.params[0];

    if (!isValidNickname(nickname))
    {
        sendServerReply(clientFd,
                        "432",
                        replyNick + " " + nickname,
                        "Erroneous nickname");
        return;
    }

    if (isNicknameInUse(nickname, clientFd))
    {
        sendServerReply(clientFd,
                        "433",
                        replyNick + " " + nickname,
                        "Nickname is already in use");
        return;
    }

    client->setNickname(nickname);

    std::cout << "NICK set for fd "
              << clientFd << ": " << nickname << std::endl;

    tryRegisterClient(clientFd);
}

void Server::handleUser(int clientFd, const Command &cmd)
{
    Client *client = findClientByFd(clientFd);

    if (client == NULL)
        return;

    if (client->isRegistered())
    {
        sendServerReply(clientFd,
                        "462",
                        getReplyNickname(*client),
                        "You may not reregister");
        return;
    }

    if (cmd.params.size() < 4)
    {
        sendServerReply(clientFd,
                "461",
                getReplyNickname(*client) + " USER",
                "Not enough parameters");
        return;
    }

    client->setUsername(cmd.params[0]);

    std::cout << "USER set for fd "
              << clientFd << ": " << cmd.params[0] << std::endl;

    tryRegisterClient(clientFd);
}

void Server::tryRegisterClient(int clientFd)
{
    Client *client = findClientByFd(clientFd);

    if (client == NULL)
        return;

    if (client->isRegistered())
        return;

    if (!client->isPassAccepted())
        return;

    if (client->getNickname().empty())
        return;

    if (client->getUsername().empty())
        return;

    client->setRegistered(true);

    sendServerReply(clientFd,
                    "001",
                    client->getNickname(),
                    "Welcome to ft_irc, " + client->getNickname());

    std::cout << "Client registered: fd "
              << clientFd << " nick=" << client->getNickname()
              << " user=" << client->getUsername() << std::endl;
}

Client *Server::findClientByNickname(const std::string &nickname)
{
    std::map<int, Client>::iterator it;

    for (it = _clients.begin(); it != _clients.end(); ++it)
    {
        if (toUpper(it->second.getNickname()) == toUpper(nickname))
            return &(it->second);
    }

    return NULL;
}

Client *Server::findClientByFd(int clientFd)
{
    std::map<int, Client>::iterator it = _clients.find(clientFd);

    if (it == _clients.end())
        return NULL;

    return &(it->second);
}

std::string Server::getReplyNickname(const Client &client) const
{
    if (client.getNickname().empty())
        return "*";

    return client.getNickname();
}

void Server::sendServerReply(int clientFd,
                             const std::string &code,
                             const std::string &middle,
                             const std::string &message)
{
    sendToClient(clientFd,
                 ":server " + code + " " + middle + " :" + message + "\r\n");
}

bool Server::requireRegistered(int clientFd, Client &client)
{
    if (client.isRegistered())
        return true;
 
    sendServerReply(clientFd, "451", "*", "You have not registered");
    return false;
}

void Server::sendPrivateMessage(const Client &sender,
                                const Client &target,
                                const std::string &message)
{
    sendToClient(target.getFd(),
                 ":" + sender.getNickname() + " PRIVMSG "
                 + target.getNickname() + " :" + message + "\r\n");
}

bool Server::isValidChannelName(const std::string &channelName) const
{
    if (channelName.length() < 2)
        return false;

    char prefix = channelName[0];

    if (prefix != '#' && prefix != '&' && prefix != '+' && prefix != '!')
        return false;

    for (size_t i = 0; i < channelName.length(); i++)
    {
        if (channelName[i] == ' ' ||
            channelName[i] == ',' ||
            channelName[i] == ':' ||
            channelName[i] == 7)
            return false;
    }

    return true;
}

void Server::handleJoin(int clientFd, const Command &cmd)
{
    Client *client = findClientByFd(clientFd);

    if (client == NULL)
        return;
    
    if (!requireRegistered(clientFd, *client))
        return;
    
    std::string replyNick = getReplyNickname(*client);

    if (cmd.params.empty())
    {
        sendServerReply(clientFd,
                        "461",
                        replyNick + " JOIN",
                        "Not enough parameters");
        return;
    }

    std::string channelName = cmd.params[0];

    if (!isValidChannelName(channelName))
    {
        sendServerReply(clientFd,
                        "403",
                        replyNick + " " + channelName,
                        "No such channel");
        return;
    }

    bool channelDoesNotExist = _channels.find(channelName) == _channels.end();

    if (channelDoesNotExist)
        _channels[channelName] = Channel(channelName);

    Channel &channel = _channels[channelName];

    if (channel.hasClient(clientFd))
        return;

    channel.addClient(client);

    if (channelDoesNotExist)
        channel.addOperator(client);

    std::string joinMessage = ":" + client->getNickname() + " JOIN " + channelName + "\r\n";
    
    broadcastToChannel(channel, joinMessage, NULL);

    std::cout << client->getNickname()
              << " joined channel " << channelName << std::endl;
}

void Server::removeClientFromChannels(int clientFd)
{
    std::map<std::string, Channel>::iterator it = _channels.begin();

    while (it != _channels.end())
    {
        it->second.removeClient(clientFd);

        if (it->second.isEmpty())
        {
            std::map<std::string, Channel>::iterator toErase = it;
            ++it;
            _channels.erase(toErase);
        }
        else
            ++it;
    }
}

void Server::broadcastToChannel(Channel &channel, const std::string &message, Client *sender)
{
    const std::map<int, Client *> &clients = channel.getClients();

    std::map<int, Client*>::const_iterator it;

    for (it = clients.begin(); it != clients.end(); ++it)
    {
       Client *client = it->second; 

        if (client != NULL && client != sender)
            sendToClient(client->getFd(), message);
    }   
}

bool Server::isChannelTarget(const std::string &channelName) const
{
    if (channelName.empty())
        return false;

    char prefix = channelName[0];
    
    if (prefix != '#' && prefix != '&' && prefix != '+' && prefix != '!')
        return false;
    
    return true;
}

void Server::handlePart(int clientFd, const Command &cmd)
{
    Client *client = findClientByFd(clientFd);

    if (client == NULL)
        return;
    
    if (!requireRegistered(clientFd, *client))
        return;
    
    if (cmd.params.empty())
    {
        sendServerReply(clientFd,
                        "461",
                        client->getNickname() + " PART",
                        "Not enough parameters");
        return;
    }

    std::string channelName = cmd.params[0];

    if (!isValidChannelName(channelName))
    {
        sendServerReply(clientFd,
                        "403",
                        client->getNickname() + " " + channelName,
                        "No such channel");
        return;
    }

    bool doesChannelExist = _channels.find(channelName) != _channels.end();

    if (!doesChannelExist)
    {
        sendServerReply(clientFd,
                        "403",
                        client->getNickname() + " " + channelName,
                        "No such channel");
        return;
    }

    Channel &channel = _channels[channelName];


    if (!channel.hasClient(clientFd))
    {
        sendServerReply(clientFd,
                        "442",
                        client->getNickname() + " " + channelName,
                        "You're not on that channel");
        return;
    }

    std::string partMessage = ":" + client->getNickname() + " PART " + channelName;

    if (cmd.params.size() >= 2 && !cmd.params[1].empty())
        partMessage += " :" + cmd.params[1];

    partMessage += "\r\n";
    
    broadcastToChannel(channel, partMessage, NULL);

    channel.removeClient(clientFd);

    if (channel.isEmpty())
        _channels.erase(channelName);
}


// ! 2. handlePart() is good, but the channel lookup can be cleaner