// vim: expandtab shiftwidth=4
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <memory>
#include <filesystem>
#include <unistd.h>


// ===================================================================
// Hardcoded App List with Icons
// ===================================================================
struct App {
    std::string name;
    std::string icon_path; // Path to the icon file (e.g., "kodi.png")
    struct Variant {
        std::string command;
        std::string variant_name;
    };
    std::vector<Variant> variants;
};


std::vector<App> apps;
/*
= {
    {"Kodi", "kodi-standalone", "kodi.png"},
    {"Moonlight", "QT_SCALE_FACTOR=3 /home/martijn/moonlight-qt/app/moonlight", "moonlight.png"},
    {"Moonlight\n(Local Mesa Debug)", "QT_SCALE_FACTOR=3 meson devenv -C /home/martijn/mesa/build /home/martijn/moonlight-qt/app/moonlight", "moonlight.png"},
    {"Moonlight\n(Local Mesa Fast)", "QT_SCALE_FACTOR=3 meson devenv -C /home/martijn/mesa/build_rel /home/martijn/moonlight-qt/app/moonlight", "moonlight.png"},
    {"Sway", "sway", "sway.svg"},
    {"Shutdown", "sudo shutdown -P 0", "shutdown.png"},
    {"Reboot", "sudo reboot", "reboot.png"},
};
*/

// ===================================================================
// Helper Struct for Rendering
// ===================================================================
// This struct will hold the pre-loaded textures to avoid reloading every frame.
struct MenuItem {
    std::string name;
    SDL_Texture* icon_texture = nullptr;
    SDL_Texture* text_texture = nullptr;
    int text_width = 0;
    int text_height = 0;

    struct Variant {
        int text_width = 0;
        int text_height = 0;
        SDL_Texture* text_texture = nullptr;
    };
    std::vector<Variant> variants;

    // Destructor to automatically clean up textures
    ~MenuItem() {
        if (icon_texture) {
            SDL_DestroyTexture(icon_texture);
        }
        if (text_texture) {
            SDL_DestroyTexture(text_texture);
        }
    }
};

// ===================================================================
// Main Application
// ===================================================================
int main(int argc, char* argv[]) {
    std::filesystem::current_path(std::filesystem::path(argv[0]).parent_path());

    // Load the config file.
    std::ifstream conf("apps.conf");
    std::string line;
    apps.clear();
    while (!conf.eof()) {
        App app;
        std::getline(conf, app.name);
        std::getline(conf, app.icon_path);
        while (true) {
            std::getline(conf, line);
            if (line.empty() || conf.eof() || conf.bad()) {
                // Seperator, new app!
                apps.emplace_back(std::move(app));
                break;
            } else {
                // Variant name
                App::Variant v;
                v.variant_name = line;
                std::getline(conf, v.command);
                std::cout << "Parse variant '" << v.variant_name << "' with command: " << v.command << std::endl;
                app.variants.emplace_back(std::move(v));
            }
        }
    }



    bool loop = true;
    while (loop) {
        // 1. Initialize SDL and its subsystems
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD) < 0) {
            std::cerr << "Error: Could not initialize SDL: " << SDL_GetError() << std::endl;
            return 1;
        }

        if (TTF_Init() == -1) {
            std::cerr << "Error: Could not initialize SDL_ttf: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return 1;
        }

        // 2. Create a Fullscreen Window and Renderer
        SDL_Window* window = SDL_CreateWindow("Launcher", 0, 0, SDL_WINDOW_FULLSCREEN);
        if (!window) {
            std::cerr << "Error: Could not create window: " << SDL_GetError() << std::endl;
            // Cleanup...
            return 1;
        }

        SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        int num_modes;
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &num_modes);
        for (int i = 0; i < num_modes; ++i) {
            SDL_DisplayMode *m = modes[i];
            std::cout << "Mode: " << m->w << "x" << m->h << "@" << m->refresh_rate << std::endl;
        }
        if (num_modes > 0) {
            SDL_SetWindowFullscreenMode(window, modes[0]);
            SDL_SyncWindow(window);
        }
        SDL_HideCursor();


        SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
        if (!renderer) {
            std::cerr << "Error: Could not create renderer: " << SDL_GetError() << std::endl;
            // Cleanup...
            return 1;
        }

        int screen_w, screen_h;
        SDL_GetRenderOutputSize(renderer, &screen_w, &screen_h);


        // 3. Load Font
        TTF_Font* font = nullptr;
        const std::vector<const char*> font_paths = {
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/corefonts/arial.ttf", // Common on some systems
            "/usr/share/fonts/TTF/DejaVuSans.ttf" // Fallback path
        };

        for (const auto& path : font_paths) {
            font = TTF_OpenFont(path, 48);
            if (font) {
                std::cout << "Loaded font: " << path << std::endl;
                break;
            }
        }

        if (!font) {
            std::cerr << "Error: Could not load any system font: " << SDL_GetError() << std::endl;
            // Cleanup...
            return 1;
        }

        // --- Layout constants ---
        const int ICON_BASE_SIZE = 256;
        const int ICON_SPACING = 120;
        const float SELECTED_SCALE = 1.3f;
        const int TEXT_Y_OFFSET = 220;

        // 4. Load Resources (Icons and Text Textures)
        std::vector<std::unique_ptr<MenuItem>> menu_items;
        SDL_Color text_color = {255, 255, 255, 255}; // White
        SDL_Texture *background = IMG_LoadTexture(renderer, "bg.png");
        SDL_SetTextureScaleMode(background, SDL_SCALEMODE_LINEAR);
        TTF_SetFontWrapAlignment(font, TTF_HORIZONTAL_ALIGN_CENTER);

        for (const auto& app : apps) {
            auto item = std::make_unique<MenuItem>();
            item->name = app.name;

            // Load Icon
            item->icon_texture = IMG_LoadTexture(renderer, app.icon_path.c_str());
            if (!item->icon_texture) {
                std::cerr << "Warning: Could not load icon " << app.icon_path << ": " << SDL_GetError() << std::endl;
            }

            // Create Text Texture
            SDL_Surface* text_surface = TTF_RenderText_Blended_Wrapped(
                    font, app.name.c_str(), app.name.length(),
                    text_color, int(ICON_BASE_SIZE * 1.2)
            );
            if (text_surface) {
                item->text_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
                item->text_width = text_surface->w;
                item->text_height = text_surface->h;
                SDL_DestroySurface(text_surface);
            } else {
                 std::cerr << "Warning: Could not render text for " << app.name << ": " << SDL_GetError() << std::endl;
            }

            for (size_t vi = 0; vi < app.variants.size(); ++vi) {
                const std::string &vname = app.variants[vi].variant_name;
                MenuItem::Variant v;
                SDL_Surface* text_surface = TTF_RenderText_Blended_Wrapped(
                        font, vname.c_str(), vname.length(),
                        text_color, int(ICON_BASE_SIZE * 1.2)
                );
                v.text_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
                v.text_width = text_surface->w;
                v.text_height = text_surface->h;
                item->variants.push_back(v);
            }

            menu_items.push_back(std::move(item));
        }


        // --- Gamepad Setup ---
        std::vector<SDL_Gamepad*> gamepads;
        int num_joysticks = 0;
        SDL_JoystickID* joysticks = SDL_GetJoysticks(&num_joysticks);
        if (joysticks) {
            for (int i = 0; i < num_joysticks; ++i) {
                if (SDL_IsGamepad(joysticks[i])) {
                    SDL_Gamepad* pad = SDL_OpenGamepad(joysticks[i]);
                    if (pad) {
                        gamepads.push_back(pad);
                        std::cout << "Gamepad opened: " << SDL_GetGamepadName(pad) << std::endl;
                    }
                }
            }
            SDL_free(joysticks);
        }


        // 5. Main Loop
        bool running = true;
        int selected_app_index = 0;
        int selected_app_variant = 0;
        bool selecting_variant = 0;
        SDL_Event event;
        float scroll_accum = 0.0f;


        // Calculate total width for centering
        const int total_width = (apps.size() * ICON_BASE_SIZE) + ((apps.size() - 1) * ICON_SPACING);
        const int start_x = (screen_w - total_width) / 2;

        SDL_SetWindowKeyboardGrab(window, true);

        while (running) {
            // --- Event Handling ---
            //SDL_WaitEvent(NULL);
            SDL_PumpEvents();
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    selected_app_index = -1;
                    running = false;
                    loop = false;
                }
                if (event.type == SDL_EVENT_KEY_DOWN) {
                    switch (event.key.key) {
                        case SDLK_LEFT:
                            selected_app_index = (selected_app_index - 1 + apps.size()) % apps.size();
                            break;
                        case SDLK_RIGHT:
                            selected_app_index = (selected_app_index + 1) % apps.size();
                            break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            running = false;
                            break;
                        case SDLK_ESCAPE:
                            selected_app_index = -1; // Special value for escape
                            running = false;
                            break;
                    }
                }
                if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    scroll_accum += event.wheel.y;
                    if (scroll_accum < -0.5f) {
                        selected_app_index = (selected_app_index - 1 + apps.size()) % apps.size();
                        scroll_accum = 0.0f;
                    } else if (scroll_accum > 0.5f) {
                        selected_app_index = (selected_app_index + 1) % apps.size();
                        scroll_accum = 0.0f;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        running = false;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        selected_app_index = -1;
                        running = false;
                        loop = false;
                    }
                }
                // --- Gamepad Hotplugging and Input ---
                if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                    SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which);
                    if (pad) {
                        gamepads.push_back(pad);
                        std::cout << "Gamepad added: " << SDL_GetGamepadName(pad) << std::endl;
                    }
                }
                if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                    SDL_Gamepad* pad_to_remove = SDL_GetGamepadFromID(event.gdevice.which);
                    std::cout << "Gamepad removed: " << SDL_GetGamepadName(pad_to_remove) << std::endl;
                    // Find and remove it from our vector
                    for (size_t i = 0; i < gamepads.size(); ++i) {
                        if (gamepads[i] == pad_to_remove) {
                            gamepads.erase(gamepads.begin() + i);
                            break;
                        }
                    }
                    SDL_CloseGamepad(pad_to_remove);
                }
                if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                    switch(event.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
                            selected_app_index = (selected_app_index - 1 + apps.size()) % apps.size();
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
                            selected_app_index = (selected_app_index + 1) % apps.size();
                            break;
                        case SDL_GAMEPAD_BUTTON_SOUTH: // A button on Xbox/Switch Pro, X on PS
                            running = false;
                            break;
                        case SDL_GAMEPAD_BUTTON_EAST: // B button on Xbox, Circle on PS
                            selected_app_index = -1;
                            running = false;
                            break;
                    }
                }
            }

            // --- Drawing ---
            SDL_SetRenderDrawColor(renderer, 20, 20, 35, 255); // Dark blue background
            SDL_RenderClear(renderer);
            SDL_FRect fullscreen_rect { 0, 0, float(screen_w), float(screen_h) };
            SDL_RenderTexture(renderer, background, NULL, &fullscreen_rect);

            int current_x = start_x;
            for (int i = 0; i < menu_items.size(); ++i) {
                float scale = (i == selected_app_index) ? SELECTED_SCALE : 1.0f;
                int icon_size = static_cast<int>(ICON_BASE_SIZE * scale);

                // Center the icon vertically
                SDL_FRect icon_rect = {
                    static_cast<float>(current_x + (ICON_BASE_SIZE / 2) - (icon_size / 2)),
                    static_cast<float>((screen_h / 2) - (icon_size / 2)),
                    static_cast<float>(icon_size),
                    static_cast<float>(icon_size)
                };

                // Dim non-selected items
                Uint8 brightness = (i == selected_app_index) ? 255 : 150;
                SDL_SetTextureColorMod(menu_items[i]->icon_texture, brightness, brightness, brightness);
                SDL_SetTextureColorMod(menu_items[i]->text_texture, brightness, brightness, brightness);

                // Render Icon
                if (menu_items[i]->icon_texture) {
                    SDL_RenderTexture(renderer, menu_items[i]->icon_texture, NULL, &icon_rect);
                }

                // Render Text below the icon
                if (menu_items[i]->text_texture) {
                    float text_x = current_x + (ICON_BASE_SIZE / 2) - (menu_items[i]->text_width / 2);
                    SDL_FRect text_rect = {
                        text_x,
                        static_cast<float>((screen_h / 2) + TEXT_Y_OFFSET),
                        static_cast<float>(menu_items[i]->text_width),
                        static_cast<float>(menu_items[i]->text_height)
                    };
                    SDL_RenderTexture(renderer, menu_items[i]->text_texture, NULL, &text_rect);
                }

                current_x += ICON_BASE_SIZE + ICON_SPACING;
            }

            SDL_RenderPresent(renderer);
        }

        // 6. Cleanup SDL resources *before* launching the external app
        std::string command_to_run = "";
        if (selected_app_index >= 0 && selected_app_index < apps.size()) {
            App &app = apps[selected_app_index];
            command_to_run = app.variants[selected_app_variant].command;
        } else {
            loop = false;
        }

        // Close all open gamepads
        for (auto pad : gamepads) {
            SDL_CloseGamepad(pad);
        }
        gamepads.clear();


        menu_items.clear(); // This will trigger destructors and free textures
        TTF_CloseFont(font);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();

        SDL_Delay(250); // Some time for the drm/kms stuff to flicker the screen. Might be totally unnecessary.

        // 7. Launch the selected application
        if (!command_to_run.empty()) {
            std::cout << "Launcher: Cleaning up and executing '" << command_to_run << "'" << std::endl;
            system(command_to_run.c_str());
        } else {
            std::cout << "Launcher: Exiting gracefully." << std::endl;
        }
    }

    return 0;
}
