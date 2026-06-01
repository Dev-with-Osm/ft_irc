#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <string>
#include <vector>

struct Command
{
    std::string cmd;
    std::vector<std::string> params;
};

std::string toUpper(const std::string &str);
Command parseCommand(const std::string &line);

#endif