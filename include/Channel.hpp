#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <map>

class Client;

class Channel
{
    private:
        std::string _name;
        std::map<int, Client *> _clients;
        std::map<int, Client *> _operators;

    public:
        Channel();
        Channel(const std::string &name);
        ~Channel();

        const std::string &getName() const;

        bool hasClient(int clientFd) const;
        void addClient(Client *client);
        void removeClient(int clientFd);

        void addOperator(Client *client);
        void removeOperator(int clientFd);
        bool isOperator(int clientFd) const;

        bool isEmpty() const;

        const std::map<int, Client *> &getClients() const;
};

#endif