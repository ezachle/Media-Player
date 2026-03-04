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
