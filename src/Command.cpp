#include "../include/Command.hpp"

std::string toUpper(const std::string &str)
{
    std::string result = str;

    for (size_t i = 0; i < result.length(); i++)
    {
        if (result[i] >= 'a' && result[i] <= 'z')
            result[i] = result[i] - 32;
    }

    return result;
}

Command parseCommand(const std::string &line)
{
    Command command;
    size_t i = 0;

    while (i < line.length() && line[i] == ' ')
        i++;

    while (i < line.length() && line[i] != ' ')
    {
        command.cmd += line[i];
        i++;
    }

    command.cmd = toUpper(command.cmd);

    while (i < line.length())
    {
        while (i < line.length() && line[i] == ' ')
            i++;

        if (i >= line.length())
            break;

        if (line[i] == ':')
        {
            command.params.push_back(line.substr(i + 1));
            break;
        }

        std::string param;

        while (i < line.length() && line[i] != ' ')
        {
            param += line[i];
            i++;
        }

        command.params.push_back(param);
    }

    return command;
}