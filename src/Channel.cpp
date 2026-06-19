#include "../include/Channel.hpp"
#include "../include/Client.hpp"
#include <cstddef>

Channel::Channel()
    : _name("")
{
}

Channel::Channel(const std::string &name)
    : _name(name)
{
}

Channel::~Channel()
{
}

const std::string &Channel::getName() const
{
    return _name;
}

bool Channel::hasClient(int clientFd) const
{
    return _clients.find(clientFd) != _clients.end();
}

void Channel::addClient(Client *client)
{
    if (client == NULL)
        return;

    _clients[client->getFd()] = client;
}

void Channel::removeClient(int clientFd)
{
    _clients.erase(clientFd);
}

const std::map<int, Client *> &Channel::getClients() const
{
    return _clients;
}