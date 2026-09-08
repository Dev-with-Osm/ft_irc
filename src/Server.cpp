#include "../include/Server.hpp"

#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <set>

Server::Server(const char *portArg, const char *password)
    : _port(0),
      _password(password),
      _serverFd(-1)
{
    try
    {
        _port = parsePort(portArg);

        _serverFd = createServerSocket();

        setNonBlocking(_serverFd);

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
        
    setNonBlocking(clientFd);

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
    else if (cmd.cmd == "KICK")
        handleKick(clientFd, cmd);
    else if (cmd.cmd == "INVITE")
        handleInvite(clientFd, cmd);
    else if (cmd.cmd == "TOPIC")
        handleTopic(clientFd, cmd);
    else if (cmd.cmd == "MODE")
        handleMode(clientFd, cmd);
    else
    {
        Client *client = findClientByFd(clientFd);
        std::string replyNick = "*";

        if (client != NULL)
            replyNick = getReplyNickname(*client);

        sendServerReply(clientFd,
                        "421",
                        replyNick + " " + cmd.cmd,
                        "Unknown command");
    }
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
    std::string channelMessage = ":" +
                                sender->getNickname()
                                + " PRIVMSG " +
                                it->second.getName() +
                                " :" +
                                message +
                                "\r\n";

    broadcastToChannel(it->second, channelMessage, sender);
}

void Server::handlePrivmsgToUser(const std::string &target,
                                int clientFd,
                                const std::string &senderNick,
                                Client *sender,
                                const std::string &message)
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

void Server::broadcastNickChange(int clientFd,
                                 const std::string &message)
{
    std::set<int> recipients;

    recipients.insert(clientFd);

    for (std::map<std::string, Channel>::iterator it = _channels.begin();
         it != _channels.end();
         ++it)
    {
        Channel &channel = it->second;

        if (!channel.hasClient(clientFd))
            continue;

        const std::map<int, Client *> &members = channel.getClients();

        for (std::map<int, Client *>::const_iterator memberIt = members.begin();
             memberIt != members.end();
             ++memberIt)
        {
            recipients.insert(memberIt->first);
        }
    }

    for (std::set<int>::iterator it = recipients.begin();
         it != recipients.end();
         ++it)
    {
        sendToClient(*it, message);
    }
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

    std::string oldNickname = client->getNickname();
    bool wasRegistered = client->isRegistered();

    if (wasRegistered && oldNickname != nickname)
    {
        std::string nickMessage =
            ":" + oldNickname + " NICK :" + nickname + "\r\n";

        broadcastNickChange(clientFd, nickMessage);
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

    std::string nick = client->getNickname();

    sendServerReply(clientFd,
                    "001",
                    nick,
                    "Welcome to ft_irc, " + nick);

    sendServerReply(clientFd,
                    "002",
                    nick,
                    "Your host is ft_irc, running version 1.0");

    sendServerReply(clientFd,
                    "003",
                    nick,
                    "This server was created today");

    sendToClient(clientFd,
                ":server 004 " + nick + " ft_irc 1.0 - itkol\r\n");
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

    if (channel.isInviteOnly() && !channel.isInvited(clientFd))
    {
        sendServerReply(clientFd,
                    "473",
                    replyNick + " " + channelName,
                    "Cannot join channel (+i)");
        return;
    }

    if (channel.hasKey())
    {
        if (cmd.params.size() < 2 || cmd.params[1] != channel.getKey())
        {
            sendServerReply(clientFd,
                            "475",
                            replyNick + " " + channelName,
                            "Cannot join channel (+k)");
            return;
        }
    }

    if (channel.hasUserLimit() && channel.getClients().size() >= channel.getUserLimit())
    {
        sendServerReply(clientFd,
                    "471",
                    replyNick + " " + channelName,
                    "Cannot join channel (+l)");
        return;
    }

    channel.addClient(client);

    if (channelDoesNotExist)
        channel.addOperator(client);

    std::string joinMessage = ":" + client->getNickname() + " JOIN " + channelName + "\r\n";

    channel.removeInvitedClient(clientFd);

    broadcastToChannel(channel, joinMessage, NULL);
    if (channel.getTopic().empty())
    {
        sendToClient(clientFd,
                    ":server 331 " + client->getNickname()
                    + " " + channelName
                    + " :No topic is set\r\n");
    }
    else
    {
        sendToClient(clientFd,
                    ":server 332 " + client->getNickname()
                    + " " + channelName
                    + " :" + channel.getTopic() + "\r\n");
    }
    sendNamesList(clientFd, *client, channel);
}

void Server::sendNamesList(int clientFd, const Client &client, Channel &channel)
{
    std::string namesList;
    const std::map<int, Client *> &channelClients = channel.getClients();

    std::map<int, Client *>::const_iterator it;

    for (it = channelClients.begin(); it != channelClients.end(); ++it)
    {
        Client *channelClient = it->second;

        if (channelClient == NULL)
            continue;

        if (channel.isOperator(channelClient->getFd()))
            namesList += "@";

        namesList += channelClient->getNickname();
        namesList += " ";
    }

    sendToClient(clientFd,
                 ":server 353 " + client.getNickname()
                 + " = " + channel.getName()
                 + " :" + namesList + "\r\n");

    sendToClient(clientFd,
                 ":server 366 " + client.getNickname()
                 + " " + channel.getName()
                 + " :End of /NAMES list\r\n");
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
        {
            ensureChannelHasOperator(it->second, it->first);
            ++it;
        }
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

    std::string replyNick = getReplyNickname(*client);
    
    if (cmd.params.empty())
    {
        sendServerReply(clientFd,
                        "461",
                        replyNick + " PART",
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

    std::map<std::string, Channel>::iterator it = _channels.find(channelName);

    if (it == _channels.end())
    {
        sendServerReply(clientFd,
                        "403",
                        replyNick + " " + channelName,
                        "No such channel");
        return;
    }

    Channel &channel = it->second;

    if (!channel.hasClient(clientFd))
    {
        sendServerReply(clientFd,
                        "442",
                        replyNick + " " + channelName,
                        "You're not on that channel");
        return;
    }

    std::string partMessage = ":" + replyNick + " PART " + channelName;

    if (cmd.params.size() >= 2 && !cmd.params[1].empty())
        partMessage += " :" + cmd.params[1];

    partMessage += "\r\n";
    
    broadcastToChannel(channel, partMessage, NULL);

    channel.removeClient(clientFd);

    if (channel.isEmpty())
        _channels.erase(channelName);
    else
        ensureChannelHasOperator(channel, channelName);
}

void Server::handleKick(int clientFd, const Command &cmd)
{
    Client *client = findClientByFd(clientFd);

    if (client == NULL)
        return;
    
    if (!requireRegistered(clientFd, *client))
        return;

    std::string replyNick = getReplyNickname(*client);

    if (cmd.params.size() < 2)
    {
        sendServerReply(clientFd,
                "461",
                replyNick + " KICK",
                "Not enough parameters");
        return;
    }

    std::string channelName = cmd.params[0];
    std::string targetNick = cmd.params[1];
    std::string reason;
    if (cmd.params.size() > 2)
        reason = cmd.params[2];
    
    if (!isValidChannelName(channelName))
    {
        sendServerReply(clientFd,
                        "403",
                        replyNick + " " + channelName,
                        "No such channel");
        return;
    }

    std::map<std::string, Channel>::iterator it = _channels.find(channelName);

    if (it == _channels.end())
    {
        sendServerReply(clientFd,
                        "403",
                        replyNick + " " + channelName,
                        "No such channel");
        return;
    }

    Channel &channel = it->second;

    if (!channel.hasClient(clientFd))
    {
        sendServerReply(clientFd,
                        "442",
                        replyNick + " " + channelName,
                        "You're not on that channel");
        return;
    }

    if (!channel.isOperator(clientFd))
    {
        sendServerReply(clientFd,
                        "482",
                        replyNick + " " + channelName,
                        "You're not channel operator");
        return;
    }
    
    Client *targetClient = findClientByNickname(targetNick);

    if (targetClient == NULL)
    {
        sendServerReply(clientFd,
                        "401",
                        replyNick + " " + targetNick,
                        "No such nick/channel");
        return;
    }

    if (!channel.hasClient(targetClient->getFd()))
    {
        sendServerReply(clientFd,
                        "441",
                        replyNick + " " + targetNick + " " + channelName,
                        "They aren't on that channel");
        return;
    }

    std::string message = ":" + replyNick + " KICK " + channelName + " " + targetNick;

    if (!reason.empty())
        message += " :" + reason;

    message += "\r\n";
    
    broadcastToChannel(channel, message, NULL);

    channel.removeClient(targetClient->getFd());
    
    if (channel.isEmpty())
        _channels.erase(channelName);
    else
        ensureChannelHasOperator(channel, channelName);
}

void Server::handleInvite(int clientFd, const Command &cmd)
{
    Client *client = findClientByFd(clientFd);

    if (client == NULL)
        return;

    if (!requireRegistered(clientFd, *client))
        return;
    
    std::string replyNick = getReplyNickname(*client);

    if (cmd.params.size() < 2)
    {
        sendServerReply(clientFd,
                "461",
                replyNick + " INVITE",
                "Not enough parameters");
        return;
    }

    std::string targetNick = cmd.params[0];
    std::string channelName = cmd.params[1];

    if (!isValidChannelName(channelName))
    {
        sendServerReply(clientFd,
                        "403",
                        replyNick + " " + channelName,
                        "No such channel");
        return;
    }

    Client *targetClient = findClientByNickname(targetNick);

    if (targetClient == NULL || !targetClient->isRegistered())
    {
        sendServerReply(clientFd,
                        "401",
                        replyNick + " " + targetNick,
                        "No such nick/channel");
        return;
    }

    std::map<std::string, Channel>::iterator it = _channels.find(channelName);

    if (it == _channels.end())
    {
        sendServerReply(clientFd,
                        "403",
                        replyNick + " " + channelName,
                        "No such channel");
        return;
    }

    Channel &channel = it->second;

    if (!channel.hasClient(clientFd))
    {
        sendServerReply(clientFd,
                        "442",
                        replyNick + " " + channelName,
                        "You're not on that channel");
        return;
    }

    if (!channel.isOperator(clientFd))
    {
        sendServerReply(clientFd,
                        "482",
                        replyNick + " " + channelName,
                        "You're not channel operator");
        return;
    }

    if (channel.hasClient(targetClient->getFd()))
    {
        sendServerReply(clientFd,
                        "443",
                        replyNick + " " + targetNick + " " + channelName,
                        "is already on channel");
        return;
    }

    channel.addInvitedClient(targetClient);

    std::string senderMsg = ":server 341 " + replyNick + " " + targetNick + " " + channelName + "\r\n";
    std::string targetMsg = ":" + replyNick + " INVITE " + targetNick + " " + channelName + "\r\n";

    sendToClient(clientFd, senderMsg);
    sendToClient(targetClient->getFd(), targetMsg);
    
}

void Server::handleTopic(int clientFd, const Command &cmd)
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
                replyNick + " TOPIC",
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

    std::map<std::string, Channel>::iterator it = _channels.find(channelName);

    if (it == _channels.end())
    {
        sendServerReply(clientFd,
                        "403",
                        replyNick + " " + channelName,
                        "No such channel");
        return;
    }

    Channel &channel = it->second;

    if (!channel.hasClient(clientFd))
    {
        sendServerReply(clientFd,
                        "442",
                        replyNick + " " + channelName,
                        "You're not on that channel");
        return;
    }

    std::string topic = channel.getTopic();
    std::string message;

    if (cmd.params.size() == 1)
    {
        if (topic.empty())
            message = ":server 331 " + replyNick + " " + channelName + " :No topic is set\r\n";
        else
            message = ":server 332 " + replyNick + " " + channelName + " :" + topic + "\r\n";
        sendToClient(clientFd, message);
    }
    else
    {
        if (channel.isTopicRestricted() && !channel.isOperator(clientFd))
        {
            sendServerReply(clientFd,
                        "482",
                        replyNick + " " + channelName,
                        "You're not channel operator");
            return;
        }
        std::string newTopic = cmd.params[1];
        channel.setTopic(newTopic);
        message = ":" + replyNick + " TOPIC " + channelName + " :" + newTopic + "\r\n";
        broadcastToChannel(channel, message, NULL);
    }
}

void Server::handleMode(int clientFd, const Command &cmd)
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
                        replyNick + " MODE",
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

    std::map<std::string, Channel>::iterator it = _channels.find(channelName);

    if (it == _channels.end())
    {
        sendServerReply(clientFd,
                        "403",
                        replyNick + " " + channelName,
                        "No such channel");
        return;
    }

    Channel &channel = it->second;

if (cmd.params.size() == 1)
{
    std::string modes = "+";
    std::string modeParams;

    if (channel.isInviteOnly())
        modes += "i";

    if (channel.isTopicRestricted())
        modes += "t";

    if (channel.hasKey())
    {
        modes += "k";
        modeParams += " " + channel.getKey();
    }

    if (channel.hasUserLimit())
    {
        modes += "l";

        std::stringstream ss;
        ss << channel.getUserLimit();

        modeParams += " " + ss.str();
    }

    sendToClient(clientFd,
                 ":server 324 "
                 + replyNick
                 + " "
                 + channelName
                 + " "
                 + modes
                 + modeParams
                 + "\r\n");

    return;
}
    if (!channel.hasClient(clientFd))
    {
        sendServerReply(clientFd,
                        "442",
                        replyNick + " " + channelName,
                        "You're not on that channel");
        return;
    }

    if (!channel.isOperator(clientFd))
    {
        sendServerReply(clientFd,
                        "482",
                        replyNick + " " + channelName,
                        "You're not channel operator");
        return;
    }

    std::string modeString = cmd.params[1];
    char currentSign = '\0';
    char lastOutputSign = '\0';
    std::string appliedModes;
    std::string appliedParams;
    size_t paramIndex = 2;
    
    for (size_t i = 0; i < modeString.length(); i++)
    {
        char mode = modeString[i];
        
        if (mode == '+' || mode == '-')
        {
            currentSign = mode;
            continue;
        }

        if (currentSign == '\0')
        {
            sendServerReply(clientFd,
                        "472",
                        replyNick + " " + std::string(1, mode),
                        "is unknown mode char to me");
            continue;
        }
        
        if (mode == 'i')
            applyInviteOnlyMode(channel, currentSign, appliedModes, lastOutputSign);
        else if (mode == 't')
            applyTopicRestrictedMode(channel, currentSign, appliedModes, lastOutputSign);
        else if (mode == 'o')
        {
            applyOperatorMode(clientFd,
                            cmd,
                            channel,
                            channelName,
                            replyNick,
                            currentSign,
                            paramIndex,
                            appliedModes,
                            lastOutputSign,
                            appliedParams);
        }
        else if (mode == 'k')
        {
            applyKeyMode(clientFd,
                        cmd,
                        channel,
                        replyNick,
                        currentSign,
                        paramIndex,
                        appliedModes,
                        lastOutputSign,
                        appliedParams);
        }
        else if (mode == 'l')
        {
            applyLimitMode(clientFd,
                        cmd,
                        channel,
                        replyNick,
                        currentSign,
                        paramIndex,
                        appliedModes,
                        lastOutputSign,
                        appliedParams);
        }
        else
        {
            sendServerReply(clientFd,
                        "472",
                        replyNick + " " + std::string(1, mode),
                        "is unknown mode char to me");
        }
    }

    if (!appliedModes.empty())
    {
        std::string modeMessage = ":" + replyNick + " MODE " + channelName + " " + appliedModes + appliedParams + "\r\n";
        broadcastToChannel(channel, modeMessage, NULL);
    }
}

void Server::ensureChannelHasOperator(Channel &channel, const std::string &channelName)
{
    if (channel.isEmpty())
        return;

    if (channel.hasOperators())
        return;

    const std::map<int, Client *> &clients = channel.getClients();

    if (clients.empty())
        return;

    Client *newOperator = clients.begin()->second;

    if (newOperator == NULL)
        return;

    channel.addOperator(newOperator);

    std::string modeMessage = ":server MODE " + channelName
                            + " +o "
                            + newOperator->getNickname()
                            + "\r\n";

    broadcastToChannel(channel, modeMessage, NULL);
}

bool Server::parseUserLimit(const std::string &value, size_t &limit) const
{
    if (value.empty())
        return false;

    for (size_t i = 0; i < value.length(); i++)
    {
        if (value[i] < '0' || value[i] > '9')
            return false;
    }

    errno = 0;
    char *end = NULL;
    long parsed = std::strtol(value.c_str(), &end, 10);

    if (errno == ERANGE || end == value.c_str() || *end != '\0')
        return false;

    if (parsed <= 0)
        return false;

    limit = static_cast<size_t>(parsed);
    return true;
}

void Server::appendAppliedMode(std::string &appliedModes,
                               char &lastOutputSign,
                               char sign,
                               char mode) const
{
    if (lastOutputSign != sign)
    {
        appliedModes += sign;
        lastOutputSign = sign;
    }

    appliedModes += mode;
}

void Server::applyInviteOnlyMode(Channel &channel,
                                 char currentSign,
                                 std::string &appliedModes,
                                 char &lastOutputSign) const
{
    if (currentSign == '+' && !channel.isInviteOnly())
    {
        channel.setInviteOnly(true);
        appendAppliedMode(appliedModes, lastOutputSign, '+', 'i');
    }
    else if (currentSign == '-' && channel.isInviteOnly())
    {
        channel.setInviteOnly(false);
        appendAppliedMode(appliedModes, lastOutputSign, '-', 'i');
    }
}

void Server::applyTopicRestrictedMode(Channel &channel,
                                      char currentSign,
                                      std::string &appliedModes,
                                      char &lastOutputSign) const
{
    if (currentSign == '+' && !channel.isTopicRestricted())
    {
        channel.setTopicRestricted(true);
        appendAppliedMode(appliedModes, lastOutputSign, '+', 't');
    }
    else if (currentSign == '-' && channel.isTopicRestricted())
    {
        channel.setTopicRestricted(false);
        appendAppliedMode(appliedModes, lastOutputSign, '-', 't');
    }
}

void Server::applyOperatorMode(int clientFd,
                               const Command &cmd,
                               Channel &channel,
                               const std::string &channelName,
                               const std::string &replyNick,
                               char currentSign,
                               size_t &paramIndex,
                               std::string &appliedModes,
                               char &lastOutputSign,
                               std::string &appliedParams)
{
    if (paramIndex >= cmd.params.size())
    {
        sendServerReply(clientFd,
                        "461",
                        replyNick + " MODE",
                        "Not enough parameters");
        return;
    }

    std::string targetNick = cmd.params[paramIndex];
    paramIndex++;

    Client *targetClient = findClientByNickname(targetNick);

    if (targetClient == NULL || !targetClient->isRegistered())
    {
        sendServerReply(clientFd,
                        "401",
                        replyNick + " " + targetNick,
                        "No such nick/channel");
        return;
    }

    if (!channel.hasClient(targetClient->getFd()))
    {
        sendServerReply(clientFd,
                        "441",
                        replyNick + " " + targetNick + " " + channelName,
                        "They aren't on that channel");
        return;
    }

    if (currentSign == '+' && !channel.isOperator(targetClient->getFd()))
    {
        channel.addOperator(targetClient);
        appendAppliedMode(appliedModes, lastOutputSign, '+', 'o');
        appliedParams += " " + targetNick;
    }
    else if (currentSign == '-' && channel.isOperator(targetClient->getFd()))
    {
        channel.removeOperator(targetClient->getFd());
        appendAppliedMode(appliedModes, lastOutputSign, '-', 'o');
        appliedParams += " " + targetNick;
    }
}

void Server::applyKeyMode(int clientFd,
                          const Command &cmd,
                          Channel &channel,
                          const std::string &replyNick,
                          char currentSign,
                          size_t &paramIndex,
                          std::string &appliedModes,
                          char &lastOutputSign,
                          std::string &appliedParams)
{
    if (currentSign == '+')
    {
        if (paramIndex >= cmd.params.size())
        {
            sendServerReply(clientFd,
                            "461",
                            replyNick + " MODE",
                            "Not enough parameters");
            return;
        }

        std::string key = cmd.params[paramIndex];
        paramIndex++;

        if (key.empty())
        {
            sendServerReply(clientFd,
                            "461",
                            replyNick + " MODE",
                            "Not enough parameters");
            return;
        }

        if (!channel.hasKey() || channel.getKey() != key)
        {
            channel.setKey(key);
            appendAppliedMode(appliedModes, lastOutputSign, '+', 'k');
            appliedParams += " " + key;
        }
    }
    else if (currentSign == '-')
    {
        if (channel.hasKey())
        {
            channel.removeKey();
            appendAppliedMode(appliedModes, lastOutputSign, '-', 'k');
        }
    }
}

void Server::applyLimitMode(int clientFd,
                            const Command &cmd,
                            Channel &channel,
                            const std::string &replyNick,
                            char currentSign,
                            size_t &paramIndex,
                            std::string &appliedModes,
                            char &lastOutputSign,
                            std::string &appliedParams)
{
    if (currentSign == '+')
    {
        if (paramIndex >= cmd.params.size())
        {
            sendServerReply(clientFd,
                            "461",
                            replyNick + " MODE",
                            "Not enough parameters");
            return;
        }

        std::string limitValue = cmd.params[paramIndex];
        paramIndex++;

        size_t limit = 0;

        if (!parseUserLimit(limitValue, limit))
        {
            sendServerReply(clientFd,
                            "461",
                            replyNick + " MODE",
                            "Invalid limit");
            return;
        }

        if (!channel.hasUserLimit() || channel.getUserLimit() != limit)
        {
            channel.setUserLimit(limit);
            appendAppliedMode(appliedModes, lastOutputSign, '+', 'l');
            appliedParams += " " + limitValue;
        }
    }
    else if (currentSign == '-')
    {
        if (channel.hasUserLimit())
        {
            channel.removeUserLimit();
            appendAppliedMode(appliedModes, lastOutputSign, '-', 'l');
        }
    }
}

void Server::setNonBlocking(int fd)
{
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl failed");
}