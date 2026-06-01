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
    _clientBuffers[clientFd] = "";

    std::cout << "New client connected, fd = " << clientFd << std::endl;
}

void Server::removeClient(size_t &i)
{
    int clientFd = _pfds[i].fd;

    std::cout << "Client disconnected, fd = " << clientFd << std::endl;

    close(clientFd);
    _clientBuffers.erase(clientFd);
    _pfds.erase(_pfds.begin() + i);

    if (i > 0)
        i--;
}

void Server::receiveFromClient(size_t &i)
{
    int clientFd = _pfds[i].fd;
    char buffer[BUFFER_SIZE];

    int bytes = recv(clientFd, buffer, sizeof(buffer) - 1, 0);

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

    buffer[bytes] = '\0';
    _clientBuffers[clientFd] += buffer;

    std::cout << "Received chunk from fd "
              << clientFd << ": " << buffer << std::endl;

    extractCompleteLines(clientFd, _clientBuffers[clientFd]);
}

void Server::extractCompleteLines(int clientFd, std::string &clientBuffer)
{
    size_t pos;

    while ((pos = clientBuffer.find('\n')) != std::string::npos)
    {
        std::string line = clientBuffer.substr(0, pos);

        if (!line.empty() && line[line.length() - 1] == '\r')
            line.erase(line.length() - 1);

        std::cout << "Complete line from fd "
                  << clientFd << ": [" << line << "]" << std::endl;

        Command cmd = parseCommand(line);

        handleCommand(clientFd, cmd);

        clientBuffer.erase(0, pos + 1);
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
    else
        std::cout << "Unknown command: [" << cmd.cmd << "]" << std::endl;
}

void Server::handlePing(int clientFd, const Command &cmd)
{
    std::string token;

    if (!cmd.params.empty())
        token = cmd.params[0];

    sendToClient(clientFd, "PONG " + token + "\r\n");
}

void Server::handlePrivmsg(int clientFd, const Command &cmd)
{
    (void)cmd;

    sendToClient(clientFd, "PRIVMSG received\r\n");
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
    _clientBuffers.clear();
    _serverFd = -1;
}