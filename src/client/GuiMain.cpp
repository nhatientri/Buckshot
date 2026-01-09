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

// Helper for Game Screen
void ShowGameScreen(const Buckshot::GameStatePacket& s, Buckshot::NetworkClient& client, bool readOnly = false) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 19));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - 19));
    ImGui::Begin("Game Board", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
    bool amIP1 = (client.getUsername() == std::string(s.p1Name));
    
    // Aliases
    int myHp = amIP1 ? s.p1Hp : s.p2Hp;
    int oppHp = amIP1 ? s.p2Hp : s.p1Hp;
    bool myHandcuffed = amIP1 ? s.p1Handcuffed : s.p2Handcuffed;
    bool oppHandcuffed = amIP1 ? s.p2Handcuffed : s.p1Handcuffed;
    const uint8_t* myInv = amIP1 ? s.p1Inventory : s.p2Inventory;
    const uint8_t* oppInv = amIP1 ? s.p2Inventory : s.p1Inventory;
    
    // TOP: Opponent
    if (opponentTexture != 0) {
        ImGui::Image((void*)(intptr_t)opponentTexture, ImVec2(100, 100)); // Display 100x100
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::Text("OPPONENT (%s): HP [%d] | Items:", amIP1 ? s.p2Name : s.p1Name, oppHp);
    for(int i=0;i<8;++i) {
         if (oppInv[i]) {
             ImGui::SameLine(); 
             ImGui::Text("[%s]", GetItemName(oppInv[i]).c_str());
         }
    }
    if (oppHandcuffed) ImGui::TextColored(ImVec4(1,0,0,1), "HANDCUFFED");
    ImGui::EndGroup();
    
    ImGui::Separator();
    
    // CENTER: Table
    ImGui::Text("Shells in Shotgun: %d", s.shellsRemaining);
    ImGui::TextDisabled("Remaining: %d Live, %d Blank", s.liveCount, s.blankCount);
    if (s.knifeActive) ImGui::TextColored(ImVec4(1,0,0,1), "KNIFE ACTIVE (Double Damage)");
    
    ImGui::Separator();
    
    // BOTTOM: Me
    ImGui::Text("YOU (%s): HP [%d] | Items:", amIP1 ? s.p1Name : s.p2Name, myHp);
    for(int i=0;i<8;++i) {
        if (myInv[i]) {
             ImGui::SameLine(); 
             ImGui::PushID(i); 
             std::string label = GetItemName(myInv[i]);
             if (readOnly) {
                 PlaySoundButton(label.c_str()); // Click does nothing but plays sound
             } else {
                 if (PlaySoundButton(label.c_str())) {
                     client.sendMove(Buckshot::USE_ITEM, (Buckshot::ItemType)myInv[i]);
                 }
             }
             ImGui::PopID();
        }
    }
    if (myHandcuffed) ImGui::TextColored(ImVec4(1,0,0,1), "HANDCUFFED");
    
    ImGui::Separator();
    
    // ACTIONS
    if (s.gameOver) {
        ImGui::Text("GAME OVER! Winner: %s", s.winner);
        
        // Show Elo change if relevant
        std::string p1Name = s.p1Name;
        std::string p2Name = s.p2Name;
        std::string myName = client.getUsername();
        
        int myDelta = 0;
        if (myName == p1Name) myDelta = s.p1EloChange;
        else if (myName == p2Name) myDelta = s.p2EloChange;
        
        if (myDelta > 0) ImGui::TextColored(ImVec4(0,1,0,1), "Elo: +%d", myDelta);
        else if (myDelta < 0) ImGui::TextColored(ImVec4(1,0,0,1), "Elo: %d", myDelta); 
        else ImGui::Text("Elo: No Change");
        
        if (!readOnly) {
            if (PlaySoundButton("Back to Lobby")) {
                client.resetGame();
            }
            ImGui::SameLine();
            if (PlaySoundButton("Rematch")) {
                std::string myName = client.getUsername();
                std::string opponentName = (myName == std::string(s.p1Name)) ? s.p2Name : s.p1Name;
                client.resetGame();
                client.sendChallenge(opponentName);
            }
        }
    } else {
        ImGui::Text("Turn: %s", s.currentTurnUser);
        ImGui::SameLine();
        
         // Interpolate time
        auto now = std::chrono::steady_clock::now();
        auto lastUpdate = client.getLastStateUpdateTime();
        int currentVisualTime = 0;
        if (s.isPaused) {
             currentVisualTime = s.turnTimeRemaining;
        } else {
             int elapsedSinceUpdate = std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdate).count();
             currentVisualTime = s.turnTimeRemaining - elapsedSinceUpdate;
        }
        if (currentVisualTime < 0) currentVisualTime = 0;

        if (currentVisualTime <= 10) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "(%ds)", currentVisualTime);
        } else {
             ImGui::Text("(%ds)", currentVisualTime);
        }

        if (!readOnly) {
            if (client.getUsername() == std::string(s.currentTurnUser)) {
                if (PlaySoundButton("SHOOT OPPONENT")) client.sendMove(Buckshot::SHOOT_OPPONENT);
                ImGui::SameLine();
                if (PlaySoundButton("SHOOT SELF")) client.sendMove(Buckshot::SHOOT_SELF);
                
                // AI Pause Check (Opponent is "The Dealer")
                // Simplified check: since s.p2Name is "The Dealer" often
                if (std::string(s.p2Name) == "The Dealer" || std::string(s.p1Name) == "The Dealer") {
                    ImGui::SameLine();
                     if (s.isPaused) {
                         if (PlaySoundButton("RESUME")) client.sendTogglePause();
                         ImGui::TextColored(ImVec4(1, 1, 0, 1), " (GAME PAUSED)");
                     } else {
                         if (PlaySoundButton("PAUSE")) client.sendTogglePause();
                     }
                }

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.0f, 0.7f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.0f, 0.8f, 0.8f));
                if (PlaySoundButton("SURRENDER")) {
                    client.sendResign();
                }
                ImGui::PopStyleColor(3);
            } else {
                 ImGui::TextDisabled("Waiting for opponent...");
            }
        } else {
            ImGui::TextDisabled("(Replay Mode - Read Only)");
        }
    }
    
    ImGui::Separator();
    ImGui::TextWrapped("Log: %s", s.message);
    
    ImGui::End();
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
    bool showLeaderboard = false;
    bool showRules = false;
    bool searchingMatch = false;
    
    // Replay State
    bool showReplayBrowser = false;
    bool showReplayViewer = false;
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
                     
                     // Show Game Board
                     if (replayIndex >= 0 && replayIndex < data.size()) {
                         ShowGameScreen(data[replayIndex], client, true);
                     }
                 }
            }

            if (!gs.inGame || gs.state.gameOver) {
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
                    if (PlaySoundButton("Watch Replays")) {
                        client.requestReplayList();
                        showReplayBrowser = true;
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
