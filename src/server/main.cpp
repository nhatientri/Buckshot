#include <iostream>
#include <signal.h>
#include "Server.h"

int main(int argc, char** argv) {
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    
    std::cout << "Starting Buckshot Server on port " << port << "..." << std::endl;
    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to prevent crash on client disconnect
    Buckshot::Server server(port);
    server.run();
    return 0;
}
