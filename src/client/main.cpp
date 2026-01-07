#include <iostream>
#include "Client.h"

int main() {
    std::cout << "Starting Buckshot Client..." << std::endl;
    Buckshot::Client client;
    if (client.connectToServer("127.0.0.1", 8080)) {
        client.run();
    }
    return 0;
}
