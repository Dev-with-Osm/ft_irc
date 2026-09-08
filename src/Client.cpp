#include "../include/Client.hpp"

Client::Client()
    : _fd(-1),
      _buffer(""),
      _sendBuffer(""),
      _nickname(""),
      _username(""),
      _passAccepted(false),
      _registered(false)
{
}

Client::Client(int fd)
    : _fd(fd),
      _buffer(""),
      _sendBuffer(""),
      _nickname(""),
      _username(""),
      _passAccepted(false),
      _registered(false)
{
}

Client::~Client()
{
}

int Client::getFd() const
{
    return _fd;
}

std::string &Client::getBuffer()
{
    return _buffer;
}

std::string &Client::getSendBuffer()
{
    return _sendBuffer;
}

const std::string &Client::getNickname() const
{
    return _nickname;
}

const std::string &Client::getUsername() const
{
    return _username;
}

bool Client::isPassAccepted() const
{
    return _passAccepted;
}

bool Client::isRegistered() const
{
    return _registered;
}

void Client::setNickname(const std::string &nickname)
{
    _nickname = nickname;
}

void Client::setUsername(const std::string &username)
{
    _username = username;
}

void Client::setPassAccepted(bool value)
{
    _passAccepted = value;
}

void Client::setRegistered(bool value)
{
    _registered = value;
}