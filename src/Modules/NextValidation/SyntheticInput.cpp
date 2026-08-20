#include "Modules/NextValidation/SyntheticInput.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

namespace Runtime::Input::Synthetic
{
    namespace
    {
        std::string Upper(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return value;
        }

        const std::unordered_map<std::string, SDL_Keycode>& KeyAliases()
        {
            static const std::unordered_map<std::string, SDL_Keycode> aliases = {
                {"RETURN", SDLK_RETURN},
                {"ENTER", SDLK_RETURN},
                {"KP_ENTER", SDLK_KP_ENTER},
                {"ESC", SDLK_ESCAPE},
                {"ESCAPE", SDLK_ESCAPE},
                {"SPACE", SDLK_SPACE},
                {"TAB", SDLK_TAB},
                {"BACKSPACE", SDLK_BACKSPACE},
                {"GRAVE", SDLK_GRAVE},
                {"BACKQUOTE", SDLK_GRAVE},
                {"DELETE", SDLK_DELETE},
                {"DEL", SDLK_DELETE},
                {"LEFT", SDLK_LEFT},
                {"RIGHT", SDLK_RIGHT},
                {"UP", SDLK_UP},
                {"DOWN", SDLK_DOWN},
                {"LSHIFT", SDLK_LSHIFT},
                {"RSHIFT", SDLK_RSHIFT},
                {"LCTRL", SDLK_LCTRL},
                {"RCTRL", SDLK_RCTRL},
                {"LALT", SDLK_LALT},
                {"RALT", SDLK_RALT},
            };
            return aliases;
        }
    }

    SDL_Keycode ResolveKeyCode(const std::string& code)
    {
        const std::string upper = Upper(code);
        if (const auto it = KeyAliases().find(upper); it != KeyAliases().end())
        {
            return it->second;
        }

        if (upper.size() == 2 && upper[0] == 'F' && std::isdigit(static_cast<unsigned char>(upper[1])))
        {
            return SDLK_F1 + (upper[1] - '1');
        }
        if (upper.size() == 3 && upper[0] == 'F' &&
            std::isdigit(static_cast<unsigned char>(upper[1])) &&
            std::isdigit(static_cast<unsigned char>(upper[2])))
        {
            const int index = std::stoi(upper.substr(1));
            if (index >= 1 && index <= 24)
            {
                return SDLK_F1 + (index - 1);
            }
        }

        SDL_Keycode key = SDL_GetKeyFromName(code.c_str());
        if (key != SDLK_UNKNOWN)
        {
            return key;
        }
        if (upper.size() == 1)
        {
            return static_cast<SDL_Keycode>(std::tolower(static_cast<unsigned char>(upper[0])));
        }
        return SDLK_UNKNOWN;
    }

    SDL_Scancode ResolveScanCode(SDL_Keycode key, const std::string& code)
    {
        SDL_Scancode scan = SDL_GetScancodeFromKey(key, nullptr);
        if (scan != SDL_SCANCODE_UNKNOWN)
        {
            return scan;
        }
        return SDL_GetScancodeFromName(code.c_str());
    }

    SDL_Keymod ResolveModifiers(const std::vector<std::string>& mods)
    {
        SDL_Keymod result = SDL_KMOD_NONE;
        for (const std::string& mod : mods)
        {
            const std::string upper = Upper(mod);
            if (upper == "CTRL" || upper == "CONTROL")
            {
                result = static_cast<SDL_Keymod>(result | SDL_KMOD_CTRL);
            }
            else if (upper == "SHIFT")
            {
                result = static_cast<SDL_Keymod>(result | SDL_KMOD_SHIFT);
            }
            else if (upper == "ALT")
            {
                result = static_cast<SDL_Keymod>(result | SDL_KMOD_ALT);
            }
            else if (upper == "GUI" || upper == "CMD" || upper == "COMMAND" || upper == "META")
            {
                result = static_cast<SDL_Keymod>(result | SDL_KMOD_GUI);
            }
        }
        return result;
    }

    Uint8 ResolveMouseButton(const std::string& button)
    {
        const std::string upper = Upper(button);
        if (upper == "RIGHT")
        {
            return SDL_BUTTON_RIGHT;
        }
        if (upper == "MIDDLE")
        {
            return SDL_BUTTON_MIDDLE;
        }
        if (upper == "X1")
        {
            return SDL_BUTTON_X1;
        }
        if (upper == "X2")
        {
            return SDL_BUTTON_X2;
        }
        return SDL_BUTTON_LEFT;
    }

    void PushKey(SDL_WindowID windowId, SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mods, bool down)
    {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = windowId;
        event.key.key = key;
        event.key.scancode = scancode;
        event.key.mod = mods;
        event.key.down = down;
        event.key.repeat = false;
        SDL_PushEvent(&event);
    }

    void PushKeyPress(SDL_WindowID windowId, SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mods)
    {
        PushKey(windowId, key, scancode, mods, true);
        PushKey(windowId, key, scancode, mods, false);
    }

    void PushText(SDL_WindowID windowId, const std::string& utf8)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.windowID = windowId;
        event.text.text = SDL_strdup(utf8.c_str());
        SDL_PushEvent(&event);
    }

    void PushMouseMove(SDL_Window* window, FPoint from, FPoint to)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = window ? SDL_GetWindowID(window) : 0;
        event.motion.x = to.x;
        event.motion.y = to.y;
        event.motion.xrel = to.x - from.x;
        event.motion.yrel = to.y - from.y;
        SDL_PushEvent(&event);
    }

    void PushMouseButton(SDL_WindowID windowId, FPoint at, Uint8 button, bool down, Uint8 clicks)
    {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = windowId;
        event.button.button = button;
        event.button.down = down;
        event.button.clicks = clicks;
        event.button.x = at.x;
        event.button.y = at.y;
        SDL_PushEvent(&event);
    }

    void PushMouseWheel(SDL_WindowID windowId, FPoint at, float x, float y)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = windowId;
        event.wheel.x = x;
        event.wheel.y = y;
        event.wheel.mouse_x = at.x;
        event.wheel.mouse_y = at.y;
        SDL_PushEvent(&event);
    }
}
