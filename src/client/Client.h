#pragma once
#include <string>

namespace Buckshot {

class Client {
public:
    bool connectToServer(const std::string& ip, int port);
    void run();

private:
    int socketFd;
    bool running;
};

}
