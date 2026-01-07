#pragma once
#include <SDL2/SDL.h>

namespace Buckshot {

class GameUI {
public:
    void init();
    void render();
    void cleanup();
};

}
