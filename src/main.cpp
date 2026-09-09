#include <iostream>
#include <stdexcept>
#include <signal.h>
#include "../include/Server.hpp"

volatile sig_atomic_t g_stop = 0;

void handleSignal(int signal)
{
    (void)signal;
    g_stop = 1;
}

int main(int argc, char const *argv[])
{
    if (argc != 3 || std::string(argv[2]).empty())
    {
        std::cout << "Usage: ./ircserv <port> <password>" << std::endl;
        return 1;
    }

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    try
    {
        Server server(argv[1], argv[2]);
        server.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}