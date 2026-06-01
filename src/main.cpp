#include <iostream>
#include <stdexcept>

#include "../include/Server.hpp"

int main(int argc, char const *argv[])
{
    if (argc != 3)
    {
        std::cout << "Usage: ./ircserv <port> <password>" << std::endl;
        return 1;
    }

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