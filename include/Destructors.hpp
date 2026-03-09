extern "C"{
#include <SDL3/SDL.h>
#include <ffmpeg/libavcodec/avcodec.h>
#include <ffmpeg/libswscale/swscale.h>
#include <ffmpeg/libswresample/swresample.h>
}

// With the usage of the Functors below, allows us to cleanly
// free pointers to Ffmpeg and SDL, no need to manually free
// memory
struct SDLDestructors {
    void operator()(SDL_Window *p) { if(p) SDL_DestroyWindow(p); }
    void operator()(SDL_Renderer *p) { if(p) SDL_DestroyRenderer(p); }
    void operator()(SDL_Texture *p) { if(p) SDL_DestroyTexture(p); }
    void operator()(SDL_AudioStream *p) { if(p) SDL_DestroyAudioStream(p); }
};
template <typename T>
using SDLPtr = std::unique_ptr<T, SDLDestructors>;

struct FfmpegDestructors {
    void operator()(AVCodecContext *p) { if(p) avcodec_free_context(&p); }
    void operator()(AVFormatContext *p) { if(p) avformat_free_context(p); }
    void operator()(AVPacket *p) { if(p) av_packet_free(&p); }
    void operator()(AVFrame *p) { if(p) av_frame_free(&p); }
    void operator()(SwsContext *p) { if(p) sws_freeContext(p); }
    void operator()(SwrContext *p) { if(p) swr_free(&p); }
};
template <typename T>
using FfmpegPtr = std::unique_ptr<T, FfmpegDestructors>;

