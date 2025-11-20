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
#include <stdlib.h>
#include <cstring>


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
    int selected_variant = 0;
};

// ===================================================================
// Main Application
// ===================================================================
int main(int argc, char* argv[]) {
    std::filesystem::current_path(std::filesystem::path(argv[0]).parent_path());

    bool windowed = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--windowed") == 0) {
            windowed = true;
        }
    }

    // Load env.conf
    {
        std::ifstream conf("env.conf");
        if (conf.is_open()) {
            std::string line;
            while (!conf.eof()) {
                std::getline(conf, line);
                if (line.empty() || conf.eof() || conf.bad()) {
                    continue;
                }
                size_t pos = line.find("=");
                std::string key = line.substr(0, pos);
                std::string val = line.substr(pos + 1);
                std::cout << "Set env var '" << key << "' to '" << val.c_str() << "'\n";
                int ret = setenv(key.c_str(), val.c_str(), true);
                if (ret) {
                    std::cout << "  => failed: " << ret << std::endl;
                }
            }
        } else {
            std::cout << "No env.conf file found.\n";
        }
    }

    // Load the config file.
    std::vector<App> apps;
    {
        std::ifstream conf("apps.conf");
        if (!conf.is_open()) {
            std::cout << "No apps.conf file found. This is necessary. Will exit.\n";
            return 1;
        }
        std::string line;
        while (!conf.eof()) {
            App app;
            std::getline(conf, app.name);
            std::getline(conf, app.icon_path);

            if (conf.eof()) {
                break;
            }
            std::cout << "Parse program '" << app.name << "' with icon: " << app.icon_path << std::endl;
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
                    std::cout << "   Parse variant '" << v.variant_name << "' with command: " << v.command << std::endl;
                    app.variants.emplace_back(std::move(v));
                }
            }
        }
    }

    bool loop = true;
    while (loop) {
        // 1. Initialize SDL and its subsystems
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            std::cerr << "Error: Could not initialize SDL: " << SDL_GetError() << std::endl;
            return 1;
        }

        if (!TTF_Init()) {
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

        if (!windowed) {
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
        }


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
#if __APPLE__
            "/System/Library/Fonts/Geneva.ttf",
            "/System/Library/Fonts/NewYork.ttf"
#else
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/corefonts/arial.ttf", // Common on some systems
            "/usr/share/fonts/TTF/DejaVuSans.ttf" // Fallback path
#endif
        };

        // --- Layout constants ---
        const int ICON_BASE_SIZE = screen_h / 8;
        const int ICON_SPACING = screen_h / 12;
        const float SELECTED_SCALE = 1.3f;
        const int TEXT_Y_OFFSET = screen_h / 30;
        const int FONT_SIZE = screen_h / 40;

        for (const auto& path : font_paths) {
            font = TTF_OpenFont(path, FONT_SIZE);
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

        // 4. Load Resources (Icons and Text Textures)
        std::vector<MenuItem> menu_items;
        menu_items.reserve(apps.size());
        SDL_Color text_color = {255, 255, 255, 255}; // White
        SDL_Texture *background = IMG_LoadTexture(renderer, "bg.png");
        SDL_SetTextureScaleMode(background, SDL_SCALEMODE_LINEAR);
        TTF_SetFontWrapAlignment(font, TTF_HORIZONTAL_ALIGN_CENTER);

        for (const auto& app : apps) {
            MenuItem item;
            item.name = app.name;

            // Load Icon
            item.icon_texture = IMG_LoadTexture(renderer, app.icon_path.c_str());
            if (!item.icon_texture) {
                std::cerr << "Warning: Could not load icon " << app.icon_path << ": " << SDL_GetError() << std::endl;
            }

            // Create Text Texture
            SDL_Surface* text_surface = TTF_RenderText_Blended_Wrapped(
                    font, app.name.c_str(), app.name.length(),
                    text_color, int(ICON_BASE_SIZE * 1.2)
            );
            if (text_surface) {
                item.text_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
                item.text_width = text_surface->w;
                item.text_height = text_surface->h;
                SDL_DestroySurface(text_surface);
            } else {
                 std::cerr << "Warning: Could not render text for " << app.name << ": " << SDL_GetError() << std::endl;
            }

            for (size_t vi = 0; vi < app.variants.size(); ++vi) {
                const std::string &vname = app.variants[vi].variant_name;
                MenuItem::Variant v;
                SDL_Surface* text_surface = TTF_RenderText_Blended(
                        font, vname.c_str(), vname.length(), text_color
                );
                v.text_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
                v.text_width = text_surface->w;
                v.text_height = text_surface->h;
                item.variants.push_back(v);
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
        bool selecting_variant = 0;
        SDL_Event event;
        float scroll_accum = 0.0f;


        // Calculate total width for centering
        const int total_width = (apps.size() * ICON_BASE_SIZE) + ((apps.size() - 1) * ICON_SPACING);
        const int start_x = (screen_w - total_width) / 2;

        SDL_SetWindowKeyboardGrab(window, true);

        struct {
            bool up;
            bool down;
            bool left;
            bool right;
            bool confirm;
            bool cancel;
        } controls;

        while (running) {
            // --- Event Handling ---
            std::memset(&controls, 0, sizeof(controls));
            SDL_PumpEvents();
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    selected_app_index = -1;
                    controls.cancel = true;
                    running = false;
                    loop = false;
                }
                if (event.type == SDL_EVENT_KEY_DOWN) {
                    switch (event.key.key) {
                        case SDLK_LEFT:
                        case SDLK_H:
                            controls.left = true;
                            break;
                        case SDLK_RIGHT:
                        case SDLK_L:
                            controls.right = true;
                            break;
                        case SDLK_K:
                        case SDLK_UP:
                            controls.up = true;
                            break;
                        case SDLK_DOWN:
                        case SDLK_J:
                            controls.down = true;
                            break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            controls.confirm = true;
                            break;
                        case SDLK_ESCAPE:
                            controls.cancel = true;
                            break;
                    }
                }
                if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    scroll_accum += event.wheel.y;
                    if (scroll_accum < -0.5f) {
                        controls.left = true;
                        scroll_accum = 0.0f;
                    } else if (scroll_accum > 0.5f) {
                        controls.right = true;
                        scroll_accum = 0.0f;
                    }
                } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        controls.confirm = true;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        controls.cancel = true;
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
                            controls.left = true;
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
                            controls.right = true;
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                            controls.down = true;
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_UP:
                            controls.up = true;
                            break;
                        case SDL_GAMEPAD_BUTTON_SOUTH: // A button on Xbox/Switch Pro, X on PS
                            controls.confirm = true;
                            break;
                        case SDL_GAMEPAD_BUTTON_EAST: // B button on Xbox, Circle on PS
                            controls.cancel = true;
                            break;
                    }
                }
            }

            if (controls.cancel) {
                selected_app_index = -1; // Special value for escape
                running = false;
            } else if (controls.left) {
                selected_app_index = (selected_app_index - 1 + apps.size()) % apps.size();
            } else if (controls.right) {
                selected_app_index = (selected_app_index + 1) % apps.size();
            } else if (controls.confirm) {
                running = false;
            }
            if (selected_app_index >= 0 && selected_app_index < menu_items.size()) {
                auto &sv = menu_items[selected_app_index].selected_variant;
                int vc = menu_items[selected_app_index].variants.size();
                if (controls.up) {
                    sv = (sv - 1 + vc) % vc;
                } else if (controls.down) {
                    sv = (sv + 1) % vc;
                }
            }

            // --- Drawing ---
            SDL_SetRenderDrawColor(renderer, 20, 20, 35, 255); // Dark blue background
            SDL_RenderClear(renderer);
            SDL_FRect fullscreen_rect { 0, 0, float(screen_w), float(screen_h) };
            SDL_RenderTexture(renderer, background, NULL, &fullscreen_rect);

            int current_x = start_x;
            for (int i = 0; i < menu_items.size(); ++i) {
                MenuItem &mi = menu_items[i];
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
                SDL_SetTextureColorMod(mi.icon_texture, brightness, brightness, brightness);
                SDL_SetTextureColorMod(mi.text_texture, brightness, brightness, brightness);

                // Render Icon
                if (mi.icon_texture) {
                    SDL_RenderTexture(renderer, mi.icon_texture, NULL, &icon_rect);
                }

                // Render Text below the icon
                if (mi.text_texture) {
                    float text_x = current_x + (ICON_BASE_SIZE / 2) - (mi.text_width / 2);
                    SDL_FRect text_rect = {
                        text_x,
                        static_cast<float>((screen_h + ICON_BASE_SIZE) / 2 + TEXT_Y_OFFSET),
                        static_cast<float>(mi.text_width),
                        static_cast<float>(mi.text_height)
                    };
                    SDL_RenderTexture(renderer, mi.text_texture, NULL, &text_rect);
                }

                current_x += ICON_BASE_SIZE + ICON_SPACING;
            }

            if (selected_app_index != -1) {
                // Render variants name of the selected app
                MenuItem &mi = menu_items[selected_app_index];
                for (int vi = 0; vi < mi.variants.size(); ++vi) {
                    auto &v = mi.variants[vi];
                    if (v.text_texture) {
                        float text_x = (screen_w - v.text_width) / 2;
                        float y = screen_h * 3 / 4 + (vi - mi.selected_variant) * FONT_SIZE * 3 / 2;
                        SDL_FRect text_rect = {
                            text_x, y,
                            static_cast<float>(v.text_width),
                            static_cast<float>(v.text_height)
                        };

                        Uint8 brightness = (vi == mi.selected_variant) ? 255 : 150;
                        SDL_SetTextureColorMod(v.text_texture, brightness, brightness, brightness);
                        SDL_RenderTexture(renderer, v.text_texture, NULL, &text_rect);
                    }
                }
            }

            SDL_RenderPresent(renderer);
        }

        // 6. Cleanup SDL resources *before* launching the external app
        std::string command_to_run = "";
        if (selected_app_index >= 0 && selected_app_index < apps.size()) {
            App &app = apps[selected_app_index];
            MenuItem &mi = menu_items[selected_app_index];
            command_to_run = app.variants[mi.selected_variant].command;
        } else {
            loop = false;
        }

        // Close all open gamepads
        for (auto pad : gamepads) {
            SDL_CloseGamepad(pad);
        }
        gamepads.clear();


        for (int i = 0; i < menu_items.size(); ++i) {
            MenuItem &mi = menu_items[i];
            SDL_DestroyTexture(mi.icon_texture);
            SDL_DestroyTexture(mi.text_texture);
            for (int vi = 0; vi < mi.variants.size(); ++vi) {
                auto &v = mi.variants[vi];
                SDL_DestroyTexture(v.text_texture);
            }
        }
        menu_items.clear();
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
