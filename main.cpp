#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <poll.h>

int parsePort(const char *portArg)
{
    return std::atoi(portArg);
}

int createServerSocket()
{
    int serverFD = socket(AF_INET, SOCK_STREAM, 0);

    if (serverFD < 0)
        throw std::runtime_error("socket failed");

    return serverFD;
}

void enableReuseAddress(int serverFD)
{
    int opt = 1;

    if (setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        throw std::runtime_error("setsockopt failed");
}

sockaddr_in createServerAddress(int port)
{
    sockaddr_in addr;

    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    return addr;
}

void bindServerSocket(int serverFD, sockaddr_in &addr)
{
    if (bind(serverFD, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "bind failed: " << std::strerror(errno) << std::endl;
        throw std::runtime_error("bind failed");
    }
}

void startListening(int serverFD, int port)
{
    if (listen(serverFD, SOMAXCONN) < 0)
        throw std::runtime_error("listen failed");

    std::cout << "Server is listening on port " << port << "..." << std::endl;
    std::cout << "Waiting for clients..." << std::endl;
}

void extractCompleteLines(int clientFd, std::string &clientBuffer)
{
    size_t pos;

    while ((pos = clientBuffer.find('\n')) != std::string::npos)
    {
        std::string line = clientBuffer.substr(0, pos);

        if (!line.empty() && line[line.length() - 1] == '\r')
            line.erase(line.length() - 1);

        std::cout << "Complete line from fd "
                  << clientFd << ": [" << line << "]" << std::endl;

        if (line.substr(0, 4) == "PING")
        {
            std::string token;

            if (line.length() > 5)
                token = line.substr(5);

            std::string reply = "PONG " + token + "\r\n";

            send(clientFd, reply.c_str(), reply.length(), 0);
        }

        clientBuffer.erase(0, pos + 1);
    }
}

void addServerToPoll(int serverFD, std::vector<pollfd> &pfds)
{
    pollfd serverPoll;

    serverPoll.fd = serverFD;
    serverPoll.events = POLLIN;
    serverPoll.revents = 0;

    pfds.push_back(serverPoll);
}

void acceptNewClient(
    int serverFD,
    std::vector<pollfd> &pfds,
    std::map<int, std::string> &clientBuffers
)
{
    int clientFd = accept(serverFD, NULL, NULL);

    if (clientFd < 0)
        throw std::runtime_error("accept failed");

    pollfd clientPoll;
    clientPoll.fd = clientFd;
    clientPoll.events = POLLIN;
    clientPoll.revents = 0;

    pfds.push_back(clientPoll);
    clientBuffers[clientFd] = "";

    std::cout << "New client connected, fd = " << clientFd << std::endl;
}

void removeClient(
    size_t &i,
    std::vector<pollfd> &pfds,
    std::map<int, std::string> &clientBuffers
)
{
    int clientFd = pfds[i].fd;

    std::cout << "Client disconnected, fd = " << clientFd << std::endl;

    close(clientFd);
    clientBuffers.erase(clientFd);
    pfds.erase(pfds.begin() + i);

    if (i > 0)
        i--;
}

void receiveFromClient(
    size_t &i,
    std::vector<pollfd> &pfds,
    std::map<int, std::string> &clientBuffers
)
{
    int clientFd = pfds[i].fd;
    char buffer[256];

    int bytes = recv(clientFd, buffer, sizeof(buffer) - 1, 0);

    if (bytes < 0)
    {
        std::cerr << "recv failed for fd " << clientFd << std::endl;
        removeClient(i, pfds, clientBuffers);
        return;
    }

    if (bytes == 0)
    {
        removeClient(i, pfds, clientBuffers);
        return;
    }

    buffer[bytes] = '\0';
    clientBuffers[clientFd] += buffer;

    std::cout << "Received chunk from fd "
              << clientFd << ": " << buffer << std::endl;

    extractCompleteLines(clientFd, clientBuffers[clientFd]);
}

void runServer(
    int serverFD,
    std::vector<pollfd> &pfds,
    std::map<int, std::string> &clientBuffers
)
{
    while (true)
    {
        int ret = poll(&pfds[0], pfds.size(), -1);

        if (ret < 0)
            throw std::runtime_error("poll failed");

        for (size_t i = 0; i < pfds.size(); i++)
        {
            if (pfds[i].revents & POLLIN)
            {
                if (pfds[i].fd == serverFD)
                    acceptNewClient(serverFD, pfds, clientBuffers);
                else
                    receiveFromClient(i, pfds, clientBuffers);
            }
        }
    }
}

int main(int argc, char const *argv[])
{
    if (argc != 3)
    {
        std::cout << "Usage: ./ircserv <port> <password>" << std::endl;
        return 1;
    }

    int serverFD = -1;
    std::vector<pollfd> pfds;
    std::map<int, std::string> clientBuffers;

    try
    {
        int port = parsePort(argv[1]);

        serverFD = createServerSocket();

        enableReuseAddress(serverFD);

        sockaddr_in addr = createServerAddress(port);

        bindServerSocket(serverFD, addr);

        startListening(serverFD, port);

        addServerToPoll(serverFD, pfds);

        runServer(serverFD, pfds, clientBuffers);

        close(serverFD);
    }   
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;

        for (size_t i = 0; i < pfds.size(); i++)
            close(pfds[i].fd);

        if (pfds.empty() && serverFD != -1)
            close(serverFD);

        return 1;
    }

    return 0;
}