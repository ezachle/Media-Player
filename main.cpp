#include <iostream>
#include "VideoState.h"

int main(int argc, char **argv) {
    try {
        if( argc != 2 ) {
            std::cerr << "Usage: ./MediaPlayer <file>" << std::endl;
            exit(-1);
        }

        // Scope the media player to free the unique pointers
        // and then call SDL_Quit
        {
            // Must be called on main thread
            VideoState vs(argv[1]);

            schedule_refresh(&vs, 39);

            SDL_Event event;
            double delay = 0.0;
            bool seek = false;
            uint64_t last_time_pressed = 0;
            while(!vs.quit) {
                SDL_WaitEvent(&event);
                switch(event.type) {
                    case SDL_EVENT_QUIT:
                        vs.quit = true;
                        SDL_FlushEvents(FF_REFRESH_EVENT, FF_REFRESH_EVENT);
                        break;
                    case FF_REFRESH_EVENT:
                        vs.refresh_video();
                        break;
                    case SDL_EVENT_KEY_DOWN:
                        if(event.key.key == SDLK_RIGHT || event.key.key == SDLK_LEFT) {
                            if(!seek) {
                                seek = true;
                                delay = vs.get_master_clock();
                            }
                            delay += (event.key.key == SDLK_RIGHT) ? 10.0 : -10.0;
                            last_time_pressed = SDL_GetTicks();
                        }
                        break;
                    default:
                        break;
                }

                if(seek && (SDL_GetTicks() - last_time_pressed) > 300) {
                    seek = false;
                    last_time_pressed = 0;
                    vs.schedule_seek((int64_t)delay);
                }
            }
        }
        SDL_Quit();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return -1;
    }
}
