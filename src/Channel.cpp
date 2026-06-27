#include "../include/Channel.hpp"
#include "../include/Client.hpp"
#include <cstddef>

Channel::Channel()
    : _name(""),
      _inviteOnly(false),
      _topicRestricted(false),
      _hasKey(false),
      _key(""),
      _hasUserLimit(false),
      _userLimit(0)
{
}

Channel::Channel(const std::string &name)
    : _name(name),
      _inviteOnly(false),
      _topicRestricted(false),
      _hasKey(false),
      _key(""),
      _hasUserLimit(false),
      _userLimit(0)
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
    _operators.erase(clientFd);
    _invitedClients.erase(clientFd);
}

const std::map<int, Client *> &Channel::getClients() const
{
    return _clients;
}

void Channel::addOperator(Client *client)
{
    if (client == NULL)
        return;
    _operators[client->getFd()] = client;
}

void Channel::removeOperator(int clientFd)
{
    _operators.erase(clientFd);
}

bool Channel::isOperator(int clientFd) const
{
    return _operators.find(clientFd) != _operators.end();
}

bool Channel::isEmpty() const
{
    return _clients.empty();
}

const std::string &Channel::getTopic() const
{
    return _topic;
}

void Channel::setTopic(const std::string &topic)
{
    _topic = topic;
}

bool Channel::isInviteOnly() const
{
    return _inviteOnly;
}

void Channel::setInviteOnly(bool value)
{
    _inviteOnly = value;
}

bool Channel::isTopicRestricted() const
{
    return _topicRestricted;
}

void Channel::setTopicRestricted(bool value)
{
    _topicRestricted = value;
}

void Channel::addInvitedClient(Client *client)
{
    if (client == NULL)
        return;

    _invitedClients[client->getFd()] = client;
}

void Channel::removeInvitedClient(int clientFd)
{
    _invitedClients.erase(clientFd);
}

bool Channel::isInvited(int clientFd) const
{
    return _invitedClients.find(clientFd) != _invitedClients.end();
}

bool Channel::hasOperators() const
{
    return !_operators.empty();
}

bool Channel::hasKey() const
{
    return _hasKey;
}

const std::string &Channel::getKey() const
{
    return _key;
}

void Channel::setKey(const std::string &key)
{
    _hasKey = true;
    _key = key;
}

void Channel::removeKey()
{
    _hasKey = false;
    _key.clear();
}

bool Channel::hasUserLimit() const
{
    return _hasUserLimit;
}

size_t Channel::getUserLimit() const
{
    return _userLimit;
}

void Channel::setUserLimit(size_t limit)
{
    _hasUserLimit = true;
    _userLimit = limit;
}

void Channel::removeUserLimit()
{
    _hasUserLimit = false;
    _userLimit = 0;
}