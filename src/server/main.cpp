#include <iostream>
#include "Server.h"

int main(int argc, char** argv) {
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    
    std::cout << "Starting Buckshot Server on port " << port << "..." << std::endl;
    Buckshot::Server server(port);
    server.run();
    return 0;
}
