#pragma once
extern "C" {
#include <ffmpeg/libavcodec/avcodec.h>
#include <ffmpeg/libswscale/swscale.h>
#include <ffmpeg/libswresample/swresample.h>
#include <ffmpeg/libavformat/avformat.h>
#include <ffmpeg/libavutil/imgutils.h>
#include <ffmpeg/libavutil/time.h>
#include <SDL3/SDL.h>
}
#include <thread>
#include <memory>
#include <string>
#include "FrameQueue.h"

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

/**
 * Notifies the event loop to display the next frame
 */
#define FF_REFRESH_EVENT SDL_EVENT_USER

/*
 * Tolerance level of AV sync
 *  AV_SYNC_THRESHOLD       our minimum
 *      If it goes below our threshold, we schedule a refresh to show it ASAP
 *  AV_NO_SYNC_THRESHOLD    our maximum
 *      If it goes above the threshold, its a late frame and consider it skipped
 */
#define AV_SYNC_THRESHOLD 0.01
#define AV_NO_SYNC_THRESHOLD 10.0

/*
 * Max perecentage the code is allowed to stretch/shrink the audio
 * to match the audio
 */
#define SAMPLE_CORRECTION_PERCENT_MAX 10
#define AUDIO_DIFF_AVG_NB 20

/**
 * Size for pictq, should be a divisible by 2
 * for alignment
 */
#define VIDEO_PICTURE_QUEUE_SIZE 8

/**
 * Audio packets queue maximum size.
 */
#define MAX_AUDIOQ_SIZE (15 * 16 * 1024)

/**
 * Video packets queue maximum size.
 */
#define MAX_VIDEOQ_SIZE (15 * 256 * 1024)

typedef struct VideoPicture {
    AVFrame *frame;
    double pts;
    int width, height;
    bool in_use;
} VideoPicture;

class VideoState {
    public:
        VideoState(const char* file_path);
        ~VideoState();

        SDL_Renderer*   get_renderer() { return renderer.get(); }
        SDL_Window*     get_window() { return window.get(); }
        SDL_Texture*    get_texture() { return texture.get(); }

        AVFormatContext* get_format_context() { return file_ctxt.get(); }
        AVCodecContext*  get_video_context() { return v_ctxt.get(); }
        AVCodecContext*  get_audio_context() { return a_ctxt.get(); }
        AVChannelLayout  get_out_channel() { return out_channel; }
        SwsContext*      get_sws() { return v_sws.get(); }
        SwrContext*      get_swr() { return a_swr.get(); }

        int get_audio_stream_index() { return audio_stream_idx; }
        int get_video_stream_index() { return video_stream_idx; }

        double get_audio_clock();
        double get_master_clock();

        void queue_frame(int stream_idx, AVFrame *f);
        AVFrame* dequeue_frame(int stream_idx);

        void check_sdl(const std::string& action, int line);
        void check_av(const std::string &msg, int rc, int line);

        void pause();
        void schedule_seek(int64_t delay);
        void refresh_video();

        bool quit;
    private:
        void setup_video();
        void setup_audio();

        static void decode_packets(VideoState *vs);

        static void video_thread(VideoState *vs);
        int alloc_picture();
        int queue_picture(AVFrame *p_frame, double pts);

        int audio_decode_frame();
        void audio_callback(SDL_AudioStream *stream, int additional_amount);
        static void audio_callback_wrapper(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount);

        void video_display();

        double sync_video(AVFrame *src, double pts);
        double sync_audio(short *samples, int samples_size);
        
        void update_pictq_index();

        FfmpegPtr<AVFormatContext>          file_ctxt;
        const int                           queue_max_size;
        int                                 audio_stream_idx;
        int                                 video_stream_idx;

        SDLPtr<SDL_Window>                  window;
        SDLPtr<SDL_Renderer>                renderer;
        SDLPtr<SDL_Texture>                 texture;

        // Video
        const AVStream                     *v_stream;
        FfmpegPtr<AVCodecContext>           v_ctxt;
        FfmpegPtr<SwsContext>               v_sws;
        FrameQueue                          v_queue;

        struct VideoPicture                 pictq[VIDEO_PICTURE_QUEUE_SIZE];
        int                                 pictq_size;
        int                                 pictq_rindex;
        int                                 pictq_windex;
        std::mutex                          pictq_mutex;
        std::condition_variable             pictq_cond;

        std::mutex                          screen_mutex;

        // Audio
        SDLPtr<SDL_AudioStream>             a_sdl_stream; 
        uint8_t                            *a_buffer;
        int                                 a_buffer_idx;
        int                                 a_buffer_size;

        const AVStream                     *a_stream;
        FfmpegPtr<AVCodecContext>           a_ctxt;
        FfmpegPtr<SwrContext>               a_swr;
        FrameQueue                          a_queue;
        AVChannelLayout                     out_channel;
        AVSampleFormat                      out_fmt;

        // AV Syncing
        enum av_sync_type {
            AV_SYNC_TYPE_VIDEO,
            AV_SYNC_TYPE_AUDIO,
            AV_SYNC_TYPE_EXTERNAL,
        };
        av_sync_type                        av_type;
        double                              v_clock;
        double                              a_clock;

        bool                                is_paused = false;
        bool                                seek_pending = false;
        int                                 seek_flags;
        int64_t                             seek_delay;

        // audio syncing
        double                              audio_diff_cum; /* used for AV difference average computation */
        double                              audio_diff_avg_coef;
        double                              audio_diff_threshold;
        int                                 audio_diff_avg_count;

        int64_t                             start_time = 0;

        // Threads
        std::thread                        *video_t;
        std::thread                        *decode_t;
};

void schedule_refresh(VideoState *vs, int delay);
