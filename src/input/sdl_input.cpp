#ifdef LILTY_SDL

#include "input/sdl_input.h"

#include <SDL2/SDL.h>

namespace lilty
{
namespace input
{

void SdlKeys::poll(float dt)
{
    // SDL_PollEvent 顺带把事件队列 pump 了，后面读到的键盘状态才是这一帧的。
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_QUIT)
            quit_ = true;
        else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_q &&
                 (e.key.keysym.mod & KMOD_CTRL))
            quit_ = true;
    }

    const Uint8 *k = SDL_GetKeyboardState(nullptr);
    // WASD 和方向键都给一份：手在左边用 WASD，在右边用方向键。
    poller_.poll(ui::Key::Up, k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W], dt);
    poller_.poll(ui::Key::Down, k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S], dt);
    poller_.poll(ui::Key::Left, k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A], dt);
    poller_.poll(ui::Key::Right, k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D], dt);
    poller_.poll(ui::Key::OK, k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE], dt);
    poller_.poll(ui::Key::Back, k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_BACKSPACE], dt);
}

} // namespace input
} // namespace lilty

#endif // LILTY_SDL
