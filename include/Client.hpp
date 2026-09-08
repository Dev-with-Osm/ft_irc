#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>

class Client
{
private:
    int _fd;
    std::string _buffer;
    std::string _sendBuffer;
    std::string _nickname;
    std::string _username;
    bool _passAccepted;
    bool _registered;

public:
    Client();
    Client(int fd);
    ~Client();

    int getFd() const;

    std::string &getBuffer();
    std::string &getSendBuffer();

    const std::string &getNickname() const;
    const std::string &getUsername() const;

    bool isPassAccepted() const;
    bool isRegistered() const;

    void setNickname(const std::string &nickname);
    void setUsername(const std::string &username);
    void setPassAccepted(bool value);
    void setRegistered(bool value);
};

#endif