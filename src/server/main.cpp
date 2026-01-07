#include <iostream>
#include "Server.h"

int main() {
    std::cout << "Starting Buckshot Server..." << std::endl;
    Buckshot::Server server(8080);
    server.run();
    return 0;
}
