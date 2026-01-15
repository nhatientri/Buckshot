#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_mixer.h>
#include <SDL_image.h>
#include <iostream>
#include "NetworkClient.h"

// Audio Globals
Mix_Chunk* clickSound = nullptr;
Mix_Chunk* hoverSound = nullptr;
ImGuiID lastHoveredId = 0;

// Texture Globals
GLuint opponentTexture = 0;
GLuint texBeer = 0;
GLuint texCigs = 0;
GLuint texHandcuffs = 0;
GLuint texGlass = 0;
GLuint texKnife = 0;
GLuint texInverter = 0;
GLuint texMeds = 0;

// Audio Helper Wrapper
bool PlaySoundButton(const char* label, const ImVec2& size = ImVec2(0,0)) {
    bool pressed = ImGui::Button(label, size);
    
    if (ImGui::IsItemHovered()) {
        ImGuiID id = ImGui::GetID(label);
        if (id != lastHoveredId) {
            if (hoverSound) Mix_PlayChannel(-1, hoverSound, 0);
            lastHoveredId = id;
        }
    }
    
    if (pressed) {
        if (clickSound) Mix_PlayChannel(-1, clickSound, 0);
    }
    return pressed;
}

// Helper function to load texture from file
// Returns 0 on failure, otherwise GL texture ID
GLuint LoadTextureFromFile(const char* filename) {
    SDL_Surface* surface = IMG_Load(filename);
    if (!surface) {
        std::cerr << "Failed to load texture " << filename << ": " << IMG_GetError() << std::endl;
        return 0;
    }

    // Convert to RGBA32 to ensure compatibility
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surface); // Free the original
    if (!converted) {
        std::cerr << "Failed to convert surface: " << SDL_GetError() << std::endl;
        return 0;
    }
    surface = converted; // Use the converted one

    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload pixels to texture (Always RGBA now)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surface->w, surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);
    
    SDL_FreeSurface(surface);
    return texture_id;
}

// Helper to display generic modal
void ShowStatusModal(const std::string& msg) {
    if (ImGui::BeginPopupModal("Status", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Dummy(ImVec2(300.0f, 0.0f)); 
        float winWidth = ImGui::GetWindowSize().x;
        float textWidth = ImGui::CalcTextSize(msg.c_str()).x;
        ImGui::SetCursorPosX((winWidth - textWidth) * 0.5f);
        ImGui::Text("%s", msg.c_str());
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        float buttonWidth = 100.0f;
        ImGui::SetCursorPosX((winWidth - buttonWidth) * 0.5f);
        if (PlaySoundButton("OK", ImVec2(buttonWidth, 0))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}


std::string GetItemName(uint8_t itemType) {
    switch (itemType) {
        case Buckshot::ITEM_BEER: return "Beer";
        case Buckshot::ITEM_CIGARETTES: return "Cigarettes";
        case Buckshot::ITEM_HANDCUFFS: return "Handcuffs";
        case Buckshot::ITEM_MAGNIFYING_GLASS: return "Glass";
        case Buckshot::ITEM_KNIFE: return "Knife";
        case Buckshot::ITEM_INVERTER: return "Inverter";
        case Buckshot::ITEM_EXPIRED_MEDICINE: return "Meds";
        default: return "Unknown";
    }
}

GLuint GetItemTexture(uint8_t itemType) {
    switch (itemType) {
        case Buckshot::ITEM_BEER: return texBeer;
        case Buckshot::ITEM_CIGARETTES: return texCigs;
        case Buckshot::ITEM_HANDCUFFS: return texHandcuffs;
        case Buckshot::ITEM_MAGNIFYING_GLASS: return texGlass;
        case Buckshot::ITEM_KNIFE: return texKnife;
        case Buckshot::ITEM_INVERTER: return texInverter;
        case Buckshot::ITEM_EXPIRED_MEDICINE: return texMeds;
        default: return 0;
    }
}

// Helper to draw a specialized item card
// Returns true if clicked
bool DrawItemCard(uint8_t itemType, bool clickable, const ImVec2& size = ImVec2(80, 100)) {
    if (itemType == 0) {
        // Draw Empty Slot (Placeholder)
        ImGui::BeginGroup();
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 0.5f)); // Darker, semi-transparent
            ImGui::BeginChild("##empty", size, true, ImGuiWindowFlags_NoScrollbar);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        ImGui::EndGroup();
        return false;
    }

    std::string name = GetItemName(itemType);
    std::string label = name + "##card"; 
    GLuint tex = GetItemTexture(itemType);

    ImGui::PushID(itemType);
    bool pressed = false;

    // Style the card
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    
    // If clickable, change color on hover/active
    if (clickable) {
         if (tex != 0) {
             ImGui::PopStyleColor(); // ImageButton handles its own bg usually, or we style it
             // ImageButton is a bit distinct.
             // To make it fit 'size', we need to check internal padding. 
             // FramePadding is usually added.
             // ImGui::ImageButton returns true on click.
             // We pass independent ID.
             // ImageButton(..., size) size is size of image. We want total size of button.
             // We can use ImGui::ImageButton with size matching our expectation minus padding.
             // Or just use Image inside a Button (harder to align) or ImageButton resizing.
             
             // Simplest: ImageButton with size param.
             // Note: ImageButton API varies by version but usually:
             // ImageButton(str_id, user_texture_id, size, uv0, uv1, bg_col, tint_col)
             
             if (ImGui::ImageButton(label.c_str(), (void*)(intptr_t)tex, size)) {
                 pressed = true;
             }
         } else {
             ImGui::PopStyleColor(); 
             pressed = ImGui::Button(name.c_str(), size);
         }
         
         if (ImGui::IsItemHovered()) {
             // Tooltip
             ImGui::BeginTooltip();
             ImGui::Text("%s", name.c_str());
             ImGui::EndTooltip();
             
             ImGuiID id = ImGui::GetID(label.c_str()); // Use consistent ID source? label includes ##card
             // Actually GetID with ptr is stable.
             if (id != lastHoveredId) {
                 if (hoverSound) Mix_PlayChannel(-1, hoverSound, 0);
                 lastHoveredId = id;
             }
         }
         if (pressed && clickSound) Mix_PlayChannel(-1, clickSound, 0);
    } else {
        ImGui::BeginChild(label.c_str(), size, true, ImGuiWindowFlags_NoScrollbar);
        
        if (tex != 0) {
            // Read-only image
            ImGui::Image((void*)(intptr_t)tex, ImVec2(size.x - 16, size.y - 16)); // Padding
        } else {
            // Centered text
            ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
            ImGui::SetCursorPos(ImVec2((size.x - textSize.x) * 0.5f, (size.y - textSize.y) * 0.5f));
            ImGui::Text("%s", name.c_str());
        }
        
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    
    ImGui::PopStyleVar();
    ImGui::PopID();
    
    return pressed;
}

// Helper to draw player info box
void DrawPlayerInfo(const char* name, int hp, int elo, bool isMe) {
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    ImGui::BeginChild(isMe ? "MeInfo" : "OppInfo", ImVec2(0, 100), true);
    
    ImGui::Text("%s", name);
    ImGui::Text("Elo: %d", elo);
    ImGui::Separator();
    
    // HP Display (Hearts or Bars)
    ImGui::Text("HP: ");
    ImGui::SameLine();
    for (int i = 0; i < hp; ++i) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "LOG"); // Using 'LOG' as a placeholder symbol if no icon? 
                                                      // Actually let's use '|' or '♥' roughly
        ImGui::SameLine();
        ImGui::Text("♥");
        ImGui::SameLine();
    }
    
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

// Main Game Screen
void ShowGameScreen(const Buckshot::GameStatePacket& s, Buckshot::NetworkClient& client, bool readOnly = false) {
    ImGuiIO& io = ImGui::GetIO();
    
    // Full screen window for the game
    ImGui::SetNextWindowPos(ImVec2(0, 19));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - 19));
    ImGui::Begin("Game Board", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

    bool amIP1 = (client.getUsername() == std::string(s.p1Name));
    
    // Aliases
    int myHp = amIP1 ? s.p1Hp : s.p2Hp;
    int oppHp = amIP1 ? s.p2Hp : s.p1Hp;
    // For now assuming we don't track opponent elo in GameState packet individually unless we parse it or add it.
    // The previous code didn't show opponent Elo, so we'll omit it or placeholder.
    // Actually GameStatePacket only has p1EloChange/p2EloChange at end. 
    // We'll just show Names.
    
    bool myHandcuffed = amIP1 ? s.p1Handcuffed : s.p2Handcuffed;
    bool oppHandcuffed = amIP1 ? s.p2Handcuffed : s.p1Handcuffed;
    const uint8_t* myInv = amIP1 ? s.p1Inventory : s.p2Inventory;
    const uint8_t* oppInv = amIP1 ? s.p2Inventory : s.p1Inventory;
    
    // Layout: 2 Columns. 
    // Left: Game Board (75%)
    // Right: Logs (25%)
    
    float boardWidth = io.DisplaySize.x * 0.75f;
    
    ImGui::Columns(2, "GameSplit", false); // false = no resizing border for now if we want fixed
    ImGui::SetColumnWidth(0, boardWidth);
    
    // LEFT COLUMN: GAME BOARD
    // -----------------------
    float windowHeight = ImGui::GetWindowHeight();
    
    // 1. OPPONENT SECTION (TOP)
    ImGui::PushID("OpponentSection");
    ImGui::BeginGroup(); // Opponent Block
    {
        // Info Box
        ImGui::Text("OPPONENT (%s)", amIP1 ? s.p2Name : s.p1Name);
        ImGui::ProgressBar((float)oppHp / 5.0f, ImVec2(-1, 20), ""); // Simplified HP bar
        
        // Item Row
        ImGui::Spacing();
        // Item Row
        ImGui::Spacing();
        for(int i=0;i<6;++i) {
             ImGui::PushID(i);
             // Always draw (DrawItemCard handles 0)
             DrawItemCard(oppInv[i], false, ImVec2(70, 80)); 
             ImGui::PopID();
             ImGui::SameLine(); 
        }
        if (oppHandcuffed) ImGui::TextColored(ImVec4(1,0,0,1), "[HANDCUFFED]");
    }
    ImGui::EndGroup();
    ImGui::PopID();
    
    // 2. CENTER SECTION (TURN & ACTIONS)
    // Safely advance cursor to 35% height
    float centerTargetY = windowHeight * 0.35f;
    float currentY_Center = ImGui::GetCursorPosY();
    if (centerTargetY > currentY_Center) {
        ImGui::Dummy(ImVec2(0, centerTargetY - currentY_Center)); 
    }
    // ImGui::SetCursorPosY(windowHeight * 0.35f); // Removed unsafe call 
    ImGui::BeginGroup(); 
    {
        // Center alignment wrapper
        float availW = ImGui::GetContentRegionAvail().x;
        
        // Turn Info
        std::string turnText = std::string("Turn: ") + s.currentTurnUser;
        bool isMyTurn = (client.getUsername() == std::string(s.currentTurnUser));
        
        if (s.gameOver) {
            if (client.getUsername() == std::string(s.winner)) {
                 turnText = "YOU WIN!";
                 ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,1,0,1)); // Green
            } else {
                 turnText = "YOU LOSE!";
                 ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0,0,1)); // Red
            }
        } else {
            if (isMyTurn) {
                 turnText = "YOUR TURN";
                 ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,1,0,1)); // Green
            } else {
                 ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0,0,1)); // Red
            }
        }
        
        ImGui::SetWindowFontScale(1.5f); // Make bigger
        float textW = ImGui::CalcTextSize(turnText.c_str()).x;
        // ImGui::SetCursorPosX((boardWidth - textW) * 0.5f);
        ImGui::NewLine(); 
        ImGui::SameLine((boardWidth - textW) * 0.5f);
        ImGui::Text("%s", turnText.c_str());
        ImGui::SetWindowFontScale(1.0f); // Reset
        
        ImGui::PopStyleColor(); // Pop Color

        // AFK Timer
        if (!s.gameOver && !s.isPaused) {
            std::string timeText = "Time: " + std::to_string(s.turnTimeRemaining) + "s";
            float timeW = ImGui::CalcTextSize(timeText.c_str()).x;
            ImGui::NewLine();
            ImGui::SameLine((boardWidth - timeW) * 0.5f);
            
            if (s.turnTimeRemaining <= 10) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0,0,1)); // Red warning
            else ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1));
            
            ImGui::Text("%s", timeText.c_str());
            ImGui::PopStyleColor();
        }

        // Ammo Info
        std::string ammoText = "Shells: " + std::to_string(s.liveCount) + " Live / " + std::to_string(s.blankCount) + " Blank";
        if (s.liveCount == 0 && s.blankCount == 0) ammoText = "Shells: ???"; // or based on shellsRemaining
        
        textW = ImGui::CalcTextSize(ammoText.c_str()).x;
        // ImGui::SetCursorPosX((boardWidth - textW) * 0.5f);
        ImGui::NewLine();
        ImGui::SameLine((boardWidth - textW) * 0.5f);
        ImGui::Text("%s", ammoText.c_str());
        
        ImGui::Spacing(); ImGui::Spacing();
        
        // Action Buttons
        if (!readOnly && !s.gameOver) {
            
            // Layout buttons centered
            float btnW = 150;
            float totalBtnW = (isMyTurn) ? (btnW * 2 + 20) : 0; // Shoot Opp + Shoot Self
            
            if (isMyTurn) {
                // ImGui::SetCursorPosX((boardWidth - totalBtnW) * 0.5f);
                ImGui::NewLine();
                ImGui::SameLine((boardWidth - totalBtnW) * 0.5f);
                
                if (PlaySoundButton("SHOOT OPPONENT", ImVec2(btnW, 40))) {
                    client.sendMove(Buckshot::SHOOT_OPPONENT);
                }
                ImGui::SameLine();
                if (PlaySoundButton("SHOOT SELF", ImVec2(btnW, 40))) {
                    client.sendMove(Buckshot::SHOOT_SELF);
                }
            }
            
            // Pause Button (always visible if needed, or check logic)
            // Centered below
            ImGui::Spacing();
            const char* pauseLabel = s.isPaused ? "RESUME" : "PAUSE";
            // ImGui::SetCursorPosX((boardWidth - 100) * 0.5f);
            if (std::string(s.p2Name) == "The Dealer" || std::string(s.p1Name) == "The Dealer") { // Simple check
                 ImGui::NewLine();
                 ImGui::SameLine((boardWidth - 100) * 0.5f);
                 if (PlaySoundButton(pauseLabel, ImVec2(100, 30))) {
                     client.sendTogglePause();
                 }
            }
        }
    }
    ImGui::EndGroup();
    
    // 3. PLAYER SECTION (BOTTOM)
    // Pin to bottom safely
    // We calculate the remaining space and add a dummy if needed, or simply set cursor
    // The crash happens because we jump past the current content size.
    
    float currentY = ImGui::GetCursorPosY();
    float targetY = windowHeight - 160.0f;
    
    if (targetY > currentY) {
        ImGui::Dummy(ImVec2(0, targetY - currentY)); // Fill space explicitly
    }
    // Now we are roughly at targetY
    
    ImGui::PushID("PlayerSection");
    ImGui::BeginGroup();
    {
        // Item Row
        if (myHandcuffed) ImGui::TextColored(ImVec4(1,0,0,1), "[HANDCUFFED]");
        for(int i=0;i<6;++i) {
             ImGui::PushID(i);
             // Always draw. For Item 0, not clickable implicitly by DrawItemCard logic
             // But we pass !readOnly. DrawItemCard handles 0 -> not clickable.
             if (DrawItemCard(myInv[i], !readOnly, ImVec2(100, 120))) {
                  client.sendMove(Buckshot::USE_ITEM, (Buckshot::ItemType)myInv[i]);
             }
             ImGui::PopID();
             ImGui::SameLine(); 
        }
        ImGui::NewLine();
        
        // Info Box
        ImGui::Text("YOU (%s)", client.getUsername().c_str());
        ImGui::ProgressBar((float)myHp / 5.0f, ImVec2(-1, 20), "");
    }
    ImGui::EndGroup();
    ImGui::PopID();
    
    ImGui::NextColumn(); // Switch to Right Column
    
    // RIGHT COLUMN: SIDEBAR (LOGS & MENU)
    // -----------------------------------
    
    // Log Window (Taking up most of space)
    float sidebarW = ImGui::GetContentRegionAvail().x;
    float bottomH = 60.0f; // Space for buttons at bottom
    
    ImGui::BeginChild("LogRegion", ImVec2(0, -bottomH), true);
    ImGui::TextWrapped("%s", s.message);
    // Ideally we'd have a vector of messages to scroll, but 's.message' is just the latest state message.
    // The original code just showed "Log: %s". We'll stick to that for now or append locally?
    // Sticking to Protocol 'message' for now as that's what we have.
    ImGui::EndChild();
    
    // Bottom Action Area
    ImGui::BeginChild("ActionRegion", ImVec2(0, 0), false); // Remaining height
    if (s.gameOver) {
        if (s.winner[0] != '\0') {
             ImGui::TextWrapped("Winner: %s", s.winner);
        }
        
        if (!readOnly) {
            if (PlaySoundButton("BACK TO LOBBY", ImVec2(-1, 0))) {
                client.resetGame();
            }
            if (PlaySoundButton("REMATCH", ImVec2(-1, 0))) {
                std::string myName = client.getUsername();
                std::string opponentName = (myName == std::string(s.p1Name)) ? s.p2Name : s.p1Name;
                client.resetGame();
                client.sendChallenge(opponentName);
            }
        }
    } else {
        // In Game Surrender
        if (!readOnly) {
            ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
            if (PlaySoundButton("SURRENDER", ImVec2(-1, 40))) {
                client.sendResign();
            }
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    
    ImGui::End(); // End Game Board Window
}

void ShowRulesWindow(bool* open) {
    if (!ImGui::Begin("Game Rules", open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    
    ImGui::TextColored(ImVec4(1, 1, 0, 1), "GOAL");
    ImGui::Text("Survive the roulette and deplete the opponent's HP.");
    ImGui::Separator();
    
    ImGui::TextColored(ImVec4(1, 0, 0, 1), "SHELLS");
    ImGui::Text("Live Round: Deals 1 Damage");
    ImGui::Text("Blank Round: Safe");
    ImGui::Separator();
    
    ImGui::TextColored(ImVec4(0, 1, 1, 1), "ITEMS");
    ImGui::BulletText("Beer: Ejects the current shell without firing.");
    ImGui::BulletText("Cigarettes: Heals 1 HP.");
    ImGui::BulletText("Handcuffs: Skips the opponent's next turn.");
    ImGui::BulletText("Magnifying Glass: Reveals the current shell.");
    ImGui::BulletText("Knife: Next shot deals double damage (2).");
    ImGui::BulletText("Inverter: Flips current shell (Live <-> Blank).");
    ImGui::BulletText("Expired Medicine: 50%% chance to Heal 2 HP, 50%% chance to lose 1 HP.");
    
    ImGui::Separator();
    if (PlaySoundButton("Close")) *open = false;
    
    ImGui::End();
}

int main(int argc, char** argv) {
    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        std::cerr << "Error: " << SDL_GetError() << std::endl;
        return -1;
    }
    
    // Initialize SDL_image
    int imgFlags = IMG_INIT_PNG;
    if (!(IMG_Init(imgFlags) & imgFlags)) {
        std::cerr << "SDL_image could not initialize! IMG_Error: " << IMG_GetError() << std::endl;
    }

    // Audio Init
    Mix_Music* bgMusic = nullptr;
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "SDL_mixer could not initialize! Error: " << Mix_GetError() << std::endl;
    } else {
        clickSound = Mix_LoadWAV("assets/click.wav");
        hoverSound = Mix_LoadWAV("assets/hover.wav");
        if (!clickSound || !hoverSound) std::cerr << "Failed to load WAVs! " << Mix_GetError() << std::endl;
        
        // Background Music
        bgMusic = Mix_LoadMUS("assets/BuckshotBGM.mp3");
        if (bgMusic) {
            Mix_PlayMusic(bgMusic, -1); // Loop infinitely
            std::cout << "Playing background music..." << std::endl;
        } else {
             std::cerr << "Failed to load Music! " << Mix_GetError() << std::endl;
        }
        
        if (bgMusic) {
            Mix_PlayMusic(bgMusic, -1); // Loop infinitely
            std::cout << "Playing background music..." << std::endl;
        } else {
             std::cerr << "Failed to load Music! " << Mix_GetError() << std::endl;
        }
    }

    // GL 3.0 + GLSL 130
    // GL 3.0 + GLSL 150
    const char* glsl_version = "#version 150";
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

    // Create window with graphics context
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Buckshot Roulette Online", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Textures (Must be done after GL context is created)
    std::cout << "Loading textures..." << std::endl;
    opponentTexture = LoadTextureFromFile("assets/dealer.png");
    if (opponentTexture != 0) std::cout << "Opponent texture loaded successfully: " << opponentTexture << std::endl;

    texBeer = LoadTextureFromFile("assets/beer.png");
    texCigs = LoadTextureFromFile("assets/cigarettes.png");
    texHandcuffs = LoadTextureFromFile("assets/handcuffs.png");
    texGlass = LoadTextureFromFile("assets/magnifying_glass.png");
    texKnife = LoadTextureFromFile("assets/knife.png");
    texInverter = LoadTextureFromFile("assets/inverter.png");
    texMeds = LoadTextureFromFile("assets/medicine.png");

    // Network Client
    // Network Client
    std::string serverIp = "127.0.0.1";
    int serverPort = 8080;

    if (argc > 1) {
        serverIp = argv[1];
    }
    if (argc > 2) {
        serverPort = std::stoi(argv[2]);
    }
    
    Buckshot::NetworkClient client;
    if (!client.connectToServer(serverIp, serverPort)) {
        std::cerr << "Failed to connect to server at " << serverIp << ":" << serverPort << "!" << std::endl;
    } else {
        std::cout << "GUI: Connected to server " << serverIp << ":" << serverPort << " successfully!" << std::endl;
    }

    // State
    char usernameBuf[32] = "";
    char passwordBuf[32] = "";
    char addFriendBuf[32] = "";
    bool showLeaderboard = false;
    bool showRules = false;
    bool showFriends = false;
    bool searchingMatch = false;
    
    // Replay State
    bool showReplayBrowser = false;
    bool showReplayViewer = false;
    bool showHistory = false;
    int replayIndex = 0;
    
    std::string currentStatusMsg = "";
    
    bool done = false;
    while (!done) {
        // Poll and handle events (inputs, window resize, etc.)
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        // Reset hover tracking if nothing is hovered, but ImGui handles ID stacks.
        // We track lastHoveredId to detect NEW hover events.
        if (!ImGui::IsAnyItemHovered()) lastHoveredId = 0;

        // Check Network Status
        std::string newMsg = client.getLastMessage();
        if (!newMsg.empty()) {
            currentStatusMsg = newMsg;
            ImGui::OpenPopup("Status");
        }
        
        // Render modal if open
        ShowStatusModal(currentStatusMsg);

        // REMATCH POPUP CHECK
        std::string rematchTarget = client.getRematchTarget();
        if (!rematchTarget.empty()) {
            ImGui::OpenPopup("Rematch Offer");
        }
    
        if (ImGui::BeginPopupModal("Rematch Offer", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Player %s wants a rematch!", rematchTarget.c_str());
            ImGui::Separator();
    
            if (PlaySoundButton("ACCEPT", ImVec2(120, 0))) {
                client.sendChallenge(rematchTarget); 
                
                // Remove challenge locally so popup doesn't reopen
                auto challenges = client.getPendingChallenges();
                for (size_t i=0; i<challenges.size(); ++i) {
                    if (challenges[i] == rematchTarget) {
                        client.removeChallenge(i);
                        break;
                    }
                }
                
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (PlaySoundButton("DECLINE", ImVec2(120, 0))) {
                // Remove challenge locally
                auto challenges = client.getPendingChallenges();
                for (size_t i=0; i<challenges.size(); ++i) {
                    if (challenges[i] == rematchTarget) {
                        client.removeChallenge(i);
                        break;
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (!client.isLoggedIn()) {
            // LOGIN SCREEN
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
            
            ImGui::InputText("Username", usernameBuf, 32);
            ImGui::InputText("Password", passwordBuf, 32, ImGuiInputTextFlags_Password);
            
            if (PlaySoundButton("Login")) {
                std::cout << "GUI: Login Button Pressed" << std::endl;
                client.loginUser(usernameBuf, passwordBuf);
            }
            ImGui::SameLine();
            if (PlaySoundButton("Register")) {
                std::cout << "GUI: Register Button Pressed" << std::endl;
                client.registerUser(usernameBuf, passwordBuf);
            }
            
            if (client.loginFailed) {
                ImGui::TextColored(ImVec4(1,0,0,1), "Login/Register Failed!");
            }
            if (client.loginSuccess) {
                 // Transition handled by isLoggedIn() state
            }
            ImGui::End();
        } else {
            // MAIN APP
            Buckshot::ClientGameState gs = client.getGameState();
            
            // MENU BAR
            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("Game")) {
                    if (ImGui::MenuItem("Return to Lobby")) {
                        showHistory = false;
                        showReplayBrowser = false;
                        showReplayViewer = false;
                        showLeaderboard = false;
                        showRules = false;
                        showFriends = false;
                    }
                    if (ImGui::MenuItem("Exit")) done = true;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    if (ImGui::MenuItem("Leaderboard")) {
                        client.getLeaderboard();
                        showLeaderboard = true;
                    }
                    if (ImGui::MenuItem("Replays")) {
                        client.requestReplayList();
                        showReplayBrowser = true;
                        showReplayViewer = false;
                        showHistory = false;
                    }
                    if (ImGui::MenuItem("Game History")) {
                        client.requestHistory();
                        showHistory = true;
                        showReplayBrowser = false;
                        showReplayViewer = false;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help")) {
                    if (ImGui::MenuItem("Rules")) {
                        showRules = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::SameLine(ImGui::GetWindowWidth() - 200);
                ImGui::Text("Logged in as: %s", client.getUsername().c_str());
                ImGui::EndMainMenuBar();
            }

            if (showRules) ShowRulesWindow(&showRules);

            // LEADERBOARD
            if (showLeaderboard) {
                ImGui::Begin("Leaderboard", &showLeaderboard);
                std::string board = client.getLeaderboardData();
                ImGui::TextUnformatted(board.c_str());
                if (PlaySoundButton("Refresh")) client.getLeaderboard();
                ImGui::End();
            }
            
            // HISTORY WINDOW
            if (showHistory) {
                ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
                ImGui::Begin("Game History", &showHistory);
                if (PlaySoundButton("Refresh")) client.requestHistory();
                ImGui::Separator();
                
                ImGui::Columns(4, "history_cols"); 
                ImGui::Separator();
                ImGui::Text("Date"); ImGui::NextColumn();
                ImGui::Text("Opponent"); ImGui::NextColumn();
                ImGui::Text("Result (Elo)"); ImGui::NextColumn();
                ImGui::Text("Action"); ImGui::NextColumn();
                ImGui::Separator();
                
                auto history = client.getHistory();
                for (const auto& entry : history) {
                    ImGui::Text("%s", entry.timestamp); ImGui::NextColumn();
                    ImGui::Text("%s", entry.opponent); ImGui::NextColumn();
                    
                    std::string res(entry.result);
                    if (res == "WIN") ImGui::TextColored(ImVec4(0,1,0,1), "WIN (%+d)", entry.eloChange);
                    else ImGui::TextColored(ImVec4(1,0,0,1), "LOSS (%+d)", entry.eloChange);
                    ImGui::NextColumn();
                    
                    if (entry.replayFile[0] != '\0') {
                        std::string label = "Watch##" + std::string(entry.timestamp);
                        if (PlaySoundButton(label.c_str())) {
                            client.requestReplayDownload(entry.replayFile);
                        }
                    } else {
                        ImGui::TextDisabled("No Replay");
                    }
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
                
                if (client.hasReplayData()) {
                    showHistory = false;
                    showReplayViewer = true;
                    replayIndex = 0;
                }
                
                ImGui::End();
            }

            // FRIENDS WINDOW
            if (showFriends) {
                ImGui::Begin("Friends List", &showFriends);
                
                // Add Friend
                ImGui::InputText("Username##add", addFriendBuf, 32);
                ImGui::SameLine();
                if (PlaySoundButton("Add Friend")) {
                    if (strlen(addFriendBuf) > 0) {
                        client.sendAddFriend(addFriendBuf);
                        addFriendBuf[0] = '\0';
                    }
                }
                ImGui::Separator();
                
                if (PlaySoundButton("Refresh List")) client.requestFriendList();
                

                
                // Friends List
                auto friends = client.getFriendList();
                // Format "Name:Status"
                if (friends.empty()) ImGui::TextDisabled("No friends yet.");
                else {
                    for(const auto& f : friends) {
                        // Parse
                        size_t colon = f.find(':');
                        if (colon != std::string::npos) {
                            std::string name = f.substr(0, colon);
                            std::string status = f.substr(colon+1);
                            
                            ImGui::Text("%s", name.c_str());
                            ImGui::SameLine(150);
                            
                            ImVec4 color = ImVec4(0.7,0.7,0.7,1);
                            if (status == "ACCEPTED" || status == "ONLINE") color = ImVec4(0,1,0,1);
                            else if (status == "OFFLINE") color = ImVec4(1,0,0,1); // Red
                            else if (status == "PENDING") color = ImVec4(1,1,0,1);
                            else if (status == "SENT") color = ImVec4(0,1,1,1);
                            
                            ImGui::TextColored(color, "[%s]", status.c_str());
                            
                            // Actions based on status
                            if (status == "ACCEPTED" || status == "ONLINE" || status == "OFFLINE") {
                                // Challenge only if ONLINE? Or allow regardless and let server handle errors?
                                // User asked for status display, but implied functionality remains.
                                // If ONLINE, show Challenge.
                                if (status == "ONLINE") {
                                    ImGui::SameLine();
                                    std::string btnId = "Challenge##" + name;
                                    if (PlaySoundButton(btnId.c_str())) {
                                        client.sendChallenge(name);
                                    }
                                }
                            } else if (status == "PENDING") {
                                // Incoming request
                                ImGui::SameLine();
                                if (PlaySoundButton(("Accept##" + name).c_str())) {
                                    client.sendAcceptFriend(name);
                                    client.requestFriendList();
                                }
                            } else if (status == "SENT") {
                                // Outgoing request - Cancel?
                                // Reuse RemoveFriend to cancel
                                ImGui::SameLine();
                                if (PlaySoundButton(("Cancel##" + name).c_str())) {
                                    client.sendRemoveFriend(name);
                                    client.requestFriendList();
                                }
                            }
                            
                            // Remove Friend
                            ImGui::SameLine(350);
                            std::string delBtn = "Remove##" + name;
                            if (PlaySoundButton(delBtn.c_str())) {
                                client.sendRemoveFriend(name);
                                client.requestFriendList(); // Refresh
                            }
                        }
                    }
                }

                ImGui::End();
            }
            
            
            // REPLAY BROWSER
            if (showReplayBrowser) {
                ImGui::Begin("Replay Browser", &showReplayBrowser);
                if (PlaySoundButton("Refresh")) client.requestReplayList();
                ImGui::Separator();
                
                auto list = client.getReplayList();
                for (const auto& f : list) {
                    if (PlaySoundButton(("Watch " + f).c_str())) {
                        client.requestReplayDownload(f);
                    }
                }
                
                if (client.hasReplayData()) {
                    showReplayBrowser = false;
                    showReplayViewer = true;
                    replayIndex = 0;
                }
                ImGui::End();
            }
            
            // REPLAY VIEWER
            if (showReplayViewer) {
                 auto data = client.getReplayData();
                 if (data.empty()) {
                     ImGui::Text("Error: No Replay Data");
                     if (PlaySoundButton("Close")) showReplayViewer = false;
                 } else {
                     // Show Game Board (Render FIRST so Controls are on TOP)
                     if (replayIndex >= 0 && replayIndex < data.size()) {
                         ShowGameScreen(data[replayIndex], client, true);
                     }

                     // Playback Controls
                     ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - 60));
                     ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 60));
                     ImGui::Begin("Playback Controls", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
                     
                     if (PlaySoundButton("<< Prev") && replayIndex > 0) replayIndex--;
                     ImGui::SameLine();
                     ImGui::Text("Move %d / %lu", replayIndex + 1, data.size());
                     ImGui::SameLine();
                     if (PlaySoundButton("Next >>") && replayIndex < (int)data.size() - 1) replayIndex++;
                     ImGui::SameLine();
                     if (PlaySoundButton("Exit Replay")) showReplayViewer = false;
                     
                     ImGui::End();
                 }
            }

            if (!gs.inGame) {
                if (!showReplayViewer) { // Don't show lobby over replay
                    // LOBBY
                    ImGui::SetNextWindowPos(ImVec2(0, 19)); // Below menu bar
                    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - 19));
                    ImGui::Begin("Lobby", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
                    if (PlaySoundButton("Practice vs AI")) {
                        client.sendPlayAiRequest();
                    }
                    ImGui::SameLine();
                    
                    if (!searchingMatch) {
                        if (PlaySoundButton("Find Match")) {
                            client.sendJoinQueue();
                            searchingMatch = true;
                        }
                    } else {
                        // Flashing or distinct color
                        ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.1f, 0.6f, 0.6f));
                        if (PlaySoundButton("Searching... (Cancel)")) {
                            client.sendLeaveQueue();
                            searchingMatch = false;
                        }
                        ImGui::PopStyleColor();
                    }

                    ImGui::SameLine();
                    ImGui::SameLine();
                    ImGui::SameLine();
                    ImGui::SameLine();
                    if (PlaySoundButton("View History")) {
                        client.requestHistory();
                        showHistory = true;
                        showReplayBrowser = false;
                        showReplayViewer = false;
                        showFriends = false;
                    }
                    ImGui::SameLine();
                    if (PlaySoundButton("Friends")) {
                        client.requestFriendList();
                        showFriends = true;
                        showHistory = false;
                        showReplayBrowser = false;
                        showReplayViewer = false;
                    }

                    
                    ImGui::Separator();
                    ImGui::Text("Online Users:");
                    auto users = client.getUserList();
                    for (const auto& u : users) {
                        if (u == client.getUsername()) continue; // Don't challenge self
                        ImGui::PushID(u.c_str());
                        if (PlaySoundButton("Challenge")) {
                            client.sendChallenge(u);
                        }
                        ImGui::SameLine();
                        ImGui::Text("%s", u.c_str());
                        ImGui::PopID();
                    }
                    
                    ImGui::Separator();
                    ImGui::Text("Incoming Challenges:");
                    auto challenges = client.getPendingChallenges();
                    for (size_t i = 0; i < challenges.size(); ++i) {
                        const auto& c = challenges[i];
                        ImGui::PushID(i);
                        ImGui::Text("From: %s", c.c_str());
                        ImGui::SameLine();
                        if (PlaySoundButton("Accept")) {
                            client.acceptChallenge(c);
                            client.removeChallenge(i); 
                        }
                        ImGui::SameLine();
                        if (PlaySoundButton("Decline")) {
                            client.removeChallenge(i); 
                        }
                        ImGui::PopID();
                    }
                    
                    ImGui::End();
                }
            }
            
            if (gs.inGame && !showReplayViewer) {
                 searchingMatch = false; // Stop searching if game started
                 ShowGameScreen(gs.state, client, false);
            }
        }
        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    if (clickSound) Mix_FreeChunk(clickSound);
    if (hoverSound) Mix_FreeChunk(hoverSound);
    if (bgMusic) Mix_FreeMusic(bgMusic);
    Mix_CloseAudio();
    
    if (opponentTexture != 0) glDeleteTextures(1, &opponentTexture);
    
    IMG_Quit();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
