#include <chrono>
#include <cstring>
#include <iostream>
#include "VideoState.h"
extern "C" {
#include <ffmpeg/libavutil/time.h>
};

typedef std::chrono::milliseconds ms;

#define UNUSED(x) ((void)x)

VideoState::VideoState(const char* file_path): queue_max_size(32), v_queue(FrameQueue(32)), a_queue(FrameQueue(32)) {
   int rc = 0;
    quit = false;

    av_type = AV_SYNC_TYPE_AUDIO;

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    check_sdl("Initializing SDL", __LINE__);

    window.reset(SDL_CreateWindow("Media Player", 1280, 720, SDL_WINDOW_RESIZABLE));
    check_sdl("Creating window", __LINE__);

    renderer.reset(SDL_CreateRenderer(window.get(), nullptr));
    check_sdl("Creating renderer", __LINE__);

    AVFormatContext *ctxt = nullptr;
    rc = avformat_open_input(&ctxt, file_path, nullptr, nullptr);
    check_av("Opening file", rc, __LINE__);
    file_ctxt.reset(ctxt);

    rc = avformat_find_stream_info(ctxt, nullptr);
    check_av("Finding stream info", rc, __LINE__);
    audio_stream_idx = video_stream_idx = -1;

    for(unsigned int i = 0; i < ctxt->nb_streams; i++) {
        if(ctxt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO ) {
            audio_stream_idx = i;
            a_stream = ctxt->streams[i];
        } else if(ctxt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            v_stream= ctxt->streams[i];
        }
    }

    if( audio_stream_idx < 0 ) {
        throw std::runtime_error("audio stream is not opened");
    }

    if( video_stream_idx < 0 ) {
        throw std::runtime_error("video stream not opened");
    }

    setup_video();
    setup_audio();

    std::this_thread::sleep_for(ms(10));

    decode_t = new std::thread(decode_packets, this);
}

VideoState::~VideoState() {
    SDL_FlushEvent(FF_REFRESH_EVENT);
    pictq_cond.notify_all();
    a_queue.quit();
    v_queue.quit();
    if(video_t) video_t->join();
    if(decode_t) decode_t->join();

    for(int i = 0; i <VIDEO_PICTURE_QUEUE_SIZE; i++) {
        av_frame_free(&pictq[i].frame);
    }
    delete[] a_buffer;
}


/**
 *  Responsible for finding the AVStream, allocating memory for ffmpeg
 *  and SDL objects, and spinning off the thread that will dequeue
 *  frames to be queued into the picture queue
 */
void VideoState::setup_video() {
    int rc;
    const AVCodec *codec = avcodec_find_decoder(v_stream->codecpar->codec_id);
    v_ctxt.reset(avcodec_alloc_context3(codec));

    rc = avcodec_parameters_to_context(v_ctxt.get(), v_stream->codecpar);
    check_av("Converting parameters to context", rc, __LINE__);

    if( v_ctxt->width != v_stream->codecpar->width ) {
        v_ctxt->width = v_stream->codecpar->width;
    }

    if( v_ctxt->height != v_stream->codecpar->height ) {
        v_ctxt->height = v_stream->codecpar->height;
    }

    texture.reset(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, v_ctxt->width, v_ctxt->height));
    check_sdl("Creating texture", __LINE__);

    rc = avcodec_open2(v_ctxt.get(), codec, nullptr);
    check_av("Opening video context", rc, __LINE__);

    pictq_size = 0;
    pictq_rindex = pictq_windex = 0;

    v_sws.reset(sws_getContext(v_ctxt->width,
                               v_ctxt->height,
                               v_ctxt->pix_fmt,
                               v_ctxt->width,
                               v_ctxt->height,
                               AV_PIX_FMT_YUV420P,
                               SWS_BILINEAR,
                               nullptr, nullptr, nullptr ));

    for(int i = 0; i <VIDEO_PICTURE_QUEUE_SIZE; i++) {
        pictq[i].frame = av_frame_alloc();
        pictq[i].in_use = false;
    }

    v_clock = 0;
    current_video_pts_time = av_gettime();
    last_frame_delay = 40e-3;
    frame_timer = av_gettime() / 1000000.0;

    video_t = new std::thread(video_thread, this);
}

/**
 *  Responsible for finding the AVStream, setting up audio output,
 *  SDL audio callback
 */
void VideoState::setup_audio() {
    int rc;
    const AVCodec *codec;
    codec = avcodec_find_decoder(a_stream->codecpar->codec_id);
    a_ctxt.reset(avcodec_alloc_context3(codec));

    rc = avcodec_parameters_to_context(a_ctxt.get(), a_stream->codecpar);
    check_av("Converting parameters to context", rc, __LINE__);

    rc = avcodec_open2(a_ctxt.get(), codec, nullptr);
    check_av("Opening audio context", rc, __LINE__);

    SDL_AudioSpec wanted_spec;
    wanted_spec.channels = a_ctxt->ch_layout.nb_channels;
    wanted_spec.freq = a_ctxt->sample_rate;
    wanted_spec.format = SDL_AUDIO_S16;

    out_fmt = AV_SAMPLE_FMT_S16;

    av_channel_layout_default(&out_channel, wanted_spec.channels);
    SwrContext *swr = swr_alloc();
    check_av("swr_alloc()", (swr == nullptr), __LINE__);

    swr_alloc_set_opts2(&swr,
                        &out_channel,                        
                        out_fmt,
                        wanted_spec.freq,
                        &a_ctxt->ch_layout,
                        a_ctxt->sample_fmt,
                        a_ctxt->sample_rate,
                        0, nullptr);
    check_sdl("Opening audio device", __LINE__);

    swr_init(swr);
    check_sdl("Opening audio device", __LINE__);

    a_swr.reset(swr);

    a_buffer_size = 0;
    a_buffer_idx = 0;
    a_clock = 0;
    
    int out_linesize;
    int data_size = av_samples_get_buffer_size(&out_linesize,
                                               a_ctxt->ch_layout.nb_channels,
                                               a_ctxt->sample_rate,
                                               out_fmt,
                                               32);
    check_av("av_samples_get_buffer_size()", data_size, __LINE__);

    a_buffer = new uint8_t[data_size];

    a_sdl_stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wanted_spec, audio_callback_wrapper, this));
    check_sdl("Opening audio device", __LINE__);

    SDL_ResumeAudioStreamDevice(a_sdl_stream.get());
    check_sdl("Resuming audio device", __LINE__);
}

// Function was created to be set to the SDL Audio callback function
// and call the actual function, as C++ implies class member functions
// to have the 'this' keyword, C-style functions don't know how to
// interepret that signature
//
// SDL callback: void (*)(void *, SDL_AudioStream, int, int)
// Member function: void (VideoState::*)(void*, SDL_AudioStream, int, int)
void VideoState::audio_callback_wrapper(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    UNUSED(total_amount);
    const auto vs = reinterpret_cast<VideoState*>(userdata);
    vs->audio_callback(stream, additional_amount);
}

/**
 *  Audio callback function set for SDL that will fire off when SDL requires audio.
 *  One thing to note is that audio_decode_frame() can return more bytes than what is
 *  requested @additional_amount. When the SDL requires data again, the surplus
 *  will be added into the audio stream instead of decoding another frame.
 *  @param stream Audio output stream for SDL
 *  @param additional_amount Number of bytes that SDL requires for audio
 */
void VideoState::audio_callback(SDL_AudioStream *stream, int additional_amount) {
    int len, audio_size;
    while(additional_amount > 0){
        if(quit) return;
        if(a_buffer_idx >= a_buffer_size){
            // Entering this if-statement means its THE FIRST TIME ENTERING or we ALREADY SENT
            // DATA and are looking for more
            audio_size = audio_decode_frame();
            if(audio_size < 0) { // Output silence
                a_buffer_size = 1024;
                std::memset(&a_buffer, 0, a_buffer_size);
                std::cerr << "audio_decode_frame() error, outputting silence" << std::endl;
            } else {
                // resample the audio
                audio_size = sync_audio(reinterpret_cast<short*>(a_buffer), audio_size);
                a_buffer_size = audio_size;
            }
            // Reset the index to prepare for copying into the buffer
            a_buffer_idx = 0;
        }

        // Calculate how much of the buffer we can add into the audio stream
        int remaining = a_buffer_size - a_buffer_idx;
        len = (remaining > additional_amount) ? additional_amount : remaining;

        SDL_PutAudioStreamData(stream, (uint8_t*)a_buffer + a_buffer_idx, len);
        check_sdl("Putting audio into the stream", __LINE__);

        additional_amount -= len;
        a_buffer_idx += len;
    }
}

double VideoState::get_video_clock() {
    return current_video_pts + ((av_gettime() - current_video_pts_time) / 1000000.0);
}

/**
 * Calculates the real-time audio clock position
 * Return the PTS of the current sample subtracted
 * from the last decoded PTS
 *
 * @returns Adjusted PTS
 */
double VideoState::get_audio_clock() {
    double pts;
    double hw_buffer_size, bytes_per_sec, bytes_per_sample, sdl_queued;

    // note nb_channels for L and R for example
    //      2 because the chosen output format is SDL_AUDIO_S16
    //        16 == 2 bytes
    bytes_per_sample = out_channel.nb_channels * 2;

    // retrieve the latest PTS
    pts = a_clock;

    // Find how much is remaining in the audio buffer +
    // how much is already queued in SDL
    hw_buffer_size = a_buffer_size - a_buffer_idx;
    sdl_queued = SDL_GetAudioStreamQueued(a_sdl_stream.get());

    bytes_per_sec = a_ctxt->sample_rate * bytes_per_sample;
    if(bytes_per_sec > 0)
        // Calculate the latency needed per sec to play the audio
        //  i.e. (4096) / ((2 *2) * 44100)
        //          == ~0.02s delay
        pts -= (double)(hw_buffer_size + sdl_queued) / bytes_per_sec;

    return pts;
}

double VideoState::get_master_clock() {
    switch(av_type) {
        case AV_SYNC_TYPE_VIDEO:
            return get_video_clock();
        case AV_SYNC_TYPE_AUDIO:
            return get_audio_clock();
        case AV_SYNC_TYPE_EXTERNAL:
        default:
            return av_gettime() / (double)AV_TIME_BASE;
    }
}

/*
 * Resamples the audio frame according to the output format
 *
 * @param vs
 * @param swr
 * @param f Audio frame to be adjusted
 * @param out_fmt
 * @param audio_buf
 * @return Converted sample size in bytes required the audio buffer
 */
static int resample_audio(VideoState *vs, SwrContext *swr, AVFrame *f, AVSampleFormat out_fmt, uint8_t *audio_buf) {
    if(vs->quit) return -1;
    auto av = vs->get_audio_context();
    int line_size = 0;
    const AVChannelLayout out_channel = vs->get_out_channel();

    // Calculate the number of samples for the output buffer
    // relative to the (previous_frame + current frame)
    //  swr_get_delay will get that progressive delay
    int out_nb_samples = av_rescale_rnd(swr_get_delay(swr, f->sample_rate) + f->nb_samples,
                                        av->sample_rate,
                                        f->sample_rate, 
                                        AV_ROUND_UP);

    // Takes the raw frame and outputs it to the output buffer
    // In this case, output is SDL_AUDIO_S16
    out_nb_samples = swr_convert(swr, &audio_buf, out_nb_samples, (const uint8_t**)f->data, f->nb_samples); 

    return av_samples_get_buffer_size(&line_size, out_channel.nb_channels, out_nb_samples, out_fmt, 32);
}

/*
 * Dequeues from the audio FrameQueue, adjusts the PTS according to 
 * the adjusted audio frame, and returns the audio size
 *
 * @return Bytes to be put into the SDL Audio Stream
 */
int VideoState::audio_decode_frame() {
    int data_size;

    FfmpegPtr<AVFrame> f(dequeue_frame(audio_stream_idx));
    auto frame = f.get();
    if( frame == nullptr ) return 0;

    // Use the frame's PTS if available as it is the best thing to
    // key off of. Set the clock relative to the PTS
    //  PTS * time base
    if(frame->pts != AV_NOPTS_VALUE) {
        a_clock = av_q2d(a_stream->time_base) * frame->pts;
    }

    // shrink or resize the audio frame
    data_size = resample_audio(this, a_swr.get(), frame, out_fmt, a_buffer); 
    if(data_size <= 0) return 0;

    int bytes_per_sample = out_channel.nb_channels * 2;

    // adds delay to the audio clock in regard to the bytes each frame
    // needs
    a_clock += (double)data_size / (double)(bytes_per_sample * a_ctxt->sample_rate);

    return data_size;
}


/*
 * Responsible for dequeuing frames from the video FrameQueue,
 * calculates the PTS, and queues the frame to be ready for
 * displaying
 */
void VideoState::video_thread(VideoState *vs) {
    // PTS == Presentation Timestamp
    //  Responsible for when the user should see the image
    // DTS == Decoding Time stamp
    //  Lets the decoder know when to decode the image
    double pts = 0;
    int idx = vs->video_stream_idx;
    while(!vs->quit) {
        AVFrame *f = vs->dequeue_frame(idx);
        if(f ==  nullptr || f == NULL) continue;

        // Best effort timestamp gives us a clue on when to display the image
        // av_q2d is responsible for converting the frame to the same
        // time unit as the video stream
        pts = (f->pkt_dts != AV_NOPTS_VALUE) ? f->best_effort_timestamp : 0;
        pts *= av_q2d(vs->v_stream->time_base);

        pts = vs->sync_video(f, pts);
        vs->queue_picture(f, pts);
    }
}

/*
 * Scales the given frame according to the window and queues up said frame
 * into a picture queue
 *
 * @param p_frame Frame about to be queued
 * @param pts The time in seconds on when to present frame
 */
int VideoState::queue_picture(AVFrame *p_frame, double pts) {
    std::unique_lock<std::mutex> lock(pictq_mutex);
    while(!quit && pictq_size >= VIDEO_PICTURE_QUEUE_SIZE) {
        pictq_cond.wait_for(lock,
                            ms(40),
                            [this](){ return this->pictq_size < VIDEO_PICTURE_QUEUE_SIZE;});
    }
    lock.unlock();
    if(quit) return -1;
                        
    VideoPicture *vp = &pictq[pictq_windex];
    if(!vp->in_use || vp->frame == nullptr) {
        vp->pts = pts;
        if(alloc_picture() == -1) return -1;
    }

    int rc = sws_scale( v_sws.get(),
                        (const uint8_t* const*)p_frame->data,
                        p_frame->linesize,
                        0,
                        p_frame->height,
                        vp->frame->data,
                        vp->frame->linesize);
    
    check_av("sws_scale", rc, __LINE__);

    lock.lock();

    // We reached the end of our circ buffer, wrap around
    if(++pictq_windex >= VIDEO_PICTURE_QUEUE_SIZE) {
        pictq_windex = 0;
    }

    pictq_size++;
    vp->in_use = true;

    return 0;
}

/*
 * Allocates an image buffer respective to the out format
 * Populates frame->data and frame->linesize
 *  
 * @returns 0 on success -1 if quitting
 */
int VideoState::alloc_picture() {
    if(quit) return -1;
    int rc = 0;
    int w = v_ctxt->width;
    int h = v_ctxt->height;
    VideoPicture *vp = &pictq[pictq_windex];

    std::lock_guard<std::mutex> lock(screen_mutex);

    AVFrame *f = vp->frame;
    rc = av_image_alloc(f->data, f->linesize, w, h, AV_PIX_FMT_YUV420P, 32);
    check_av("av_image_fill_arrays()", rc, __LINE__);

    vp->width = w;
    vp->height = h;
    vp->in_use = true;

    return 0;
}

// Called when a refresh is necessary, adds the user-made event FF_REFRESH_EVENT
// into the SDL event queue. The main thread will pick up on this and refresh
// the video
static uint32_t sdl_refresh_timer_cb(void *userdata, SDL_TimerID timerID, Uint32 interval) {
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = userdata;
    SDL_PushEvent(&event);

    return 0;
}

/*
 *  Function actually responsible for displaying the video frame
 *  to the SDL Window and Renderer
 */
void VideoState::video_display() {
    if(quit) return;

    VideoPicture *vp = &pictq[pictq_rindex];
    if(!quit && vp->in_use && vp->frame != nullptr) {
        std::lock_guard<std::mutex> lock(screen_mutex);
        // Calculates the delay using the base frame rate, and
        // schedules a refresh according to the in milliseconds
        // Tells the main thread when to display next frame
        double fps = av_q2d(v_stream->r_frame_rate);
        double delay = 1.0 / fps;
        schedule_refresh(this, static_cast<int>((delay * 1000) - 10));

        auto f = vp->frame;
        SDL_UpdateYUVTexture(texture.get(), nullptr,
                             f->data[0], f->linesize[0],
                             f->data[1], f->linesize[1],
                             f->data[2], f->linesize[2]);
        check_sdl("Updating YUV texture", __LINE__);

        SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255 );
        check_sdl("Setting render color", __LINE__);

        SDL_RenderClear(renderer.get());
        check_sdl("Clearing screen", __LINE__);

        SDL_RenderTexture(renderer.get(), texture.get(), NULL, NULL);
        check_sdl("Rendering the texture", __LINE__);

        SDL_RenderPresent(renderer.get());
        check_sdl("Presenting", __LINE__);
        
        // Reset the frame in our buffer to reuse
        av_frame_unref(f);
        vp->in_use = 0;
    }
}

double VideoState::sync_audio(short *samples, int samples_size) {
    int bytes_per_sample = out_channel.nb_channels * 2;

    if(av_type != AV_SYNC_TYPE_AUDIO) {
        double diff, avg_diff;

        diff = get_audio_clock() - get_master_clock();

        // We need to determine how much of the audio data
        // we want to add or cut off
        if(fabs(diff) < AV_NO_SYNC_THRESHOLD) {
            // Exponentially Weighted Moving Average
            // This formula is used to smooth out the jitter
            audio_diff_cum = diff + audio_diff_avg_coef * audio_diff_cum; 
            if(audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                audio_diff_avg_count++;
            } else {
                avg_diff = audio_diff_cum * (1.0 - audio_diff_avg_coef);

                if(fabs(avg_diff) >= audio_diff_threshold) {
                    int wanted_size, min_size, max_size;
                    wanted_size = samples_size + ((int)(diff * a_ctxt->sample_rate) * bytes_per_sample);
                    min_size = samples_size * ((100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100);
                    max_size = samples_size * ((100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100);

                    if(wanted_size < min_size) {
                        wanted_size = min_size;
                    } else if(wanted_size > max_size) {
                        wanted_size = max_size;
                    }

                    if(wanted_size < samples_size) {
                        samples_size = wanted_size;
                    } else if(wanted_size > samples_size) {
                        int bytes_to_copy = samples_size - wanted_size;
                        
                        // ptr to the last sample
                        uint8_t *samples_end = (uint8_t*)samples + samples_size - bytes_per_sample;

                        // one past the last sample
                        uint8_t *q = samples_end + bytes_per_sample;
                        while(bytes_to_copy > 0) {
                            std::memcpy(q, samples_end, bytes_per_sample);
                            q += bytes_per_sample;
                            bytes_to_copy -= bytes_per_sample;
                        }

                        samples_size = wanted_size;
                    }
                }
            }
        } else {
            audio_diff_avg_count = 0;
            audio_diff_cum = 0;
        }
    }

    return samples_size;
}

double VideoState::sync_video(AVFrame *src, double pts) {
    double frame_delay = 0.0;
    
    if(pts != 0) {
        v_clock = pts;
    } else {
        pts = v_clock;
    }

    // Calculate the total delay to apply to our clock
    frame_delay = av_q2d(v_stream->time_base);
    // account for any repeated frames. the 0.5 is a safety precaution
    frame_delay += src->repeat_pict * (frame_delay * 0.5);
    v_clock += frame_delay;
    return pts;
}

void schedule_refresh(VideoState *vs, int delay) {
    // The passed in delay tells SDL how long to wait before calling the
    // callback
    SDL_AddTimer(delay, sdl_refresh_timer_cb, vs);
    //vs->check_sdl("Scheduling refresh", __LINE__);
    SDL_ClearError();
}

void VideoState::refresh_video() {
    if(pictq_size == 0) {
        schedule_refresh(this, 1);
        return;
    }

    if(quit) return;

    VideoPicture *vp = &pictq[pictq_rindex];

    // Calculate the difference for when it SHOULD be presented
    // versus the clock
    double diff = vp->pts - get_master_clock();

    if(diff > AV_SYNC_THRESHOLD) {
        // It's early, reschedule it using the calculated diff
        schedule_refresh(this, (int)(diff * 1000));
        return;
    } else if(diff < -AV_NO_SYNC_THRESHOLD) {
        av_log(NULL, AV_LOG_WARNING, "Late frame, dropping (%.3f)\n", diff);
    } else {
        video_display();
    }

    {
        std::lock_guard<std::mutex> lock(pictq_mutex);

        vp->in_use = false;
        if(++pictq_rindex >= VIDEO_PICTURE_QUEUE_SIZE) {
            pictq_rindex = 0;
        }

        pictq_size--;
        pictq_cond.notify_all();
    }

    schedule_refresh(this, 1);
}

void VideoState::decode_packets(VideoState *vs) {
    FfmpegPtr<AVPacket> p_pkt(av_packet_alloc());
    FfmpegPtr<AVFrame> queue_f(av_frame_alloc());
    AVCodecContext *av_ctxt = NULL;
    auto file_ctxt = vs->get_format_context();
    int audio_stream_idx = vs->get_audio_stream_index();
    int video_stream_idx = vs->get_video_stream_index();
    int rc = 0;

    while(!vs->quit) {
        if(vs->v_queue.get_size() > (size_t)MAX_VIDEOQ_SIZE / vs->queue_max_size ||
           vs->a_queue.get_size() > (size_t)MAX_AUDIOQ_SIZE / vs->queue_max_size ) {
            ms(40);
            continue;
        }

        // unreference for reuse
        av_packet_unref(p_pkt.get());
        av_frame_unref(queue_f.get());

        // Returns a reference counted packet for an AV stream
        if((rc = av_read_frame(file_ctxt, p_pkt.get())) < 0 ) {
            if(rc == AVERROR_EOF || rc == AVERROR(rc)) continue;
            vs->check_av("Reading frame", rc, __LINE__);
        }
        
        // Find the associated AVCodecContext of the packet
        if(p_pkt->stream_index == audio_stream_idx) {
            av_ctxt = vs->get_audio_context();
        } else if(p_pkt->stream_index == video_stream_idx) {
            av_ctxt = vs->get_video_context();
        } else {
            continue;
        }

        // Send the packet to the associated AVStream
        rc = avcodec_send_packet(av_ctxt, p_pkt.get());
        if(rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            // New data is needed, go fetch more through read frame
            continue;
        }
        vs->check_av("Sending packet to av stream", rc, __LINE__);

        while(rc >= 0 && !vs->quit) {
            // Exhaust all possible frames in the given packet and queue into
            // the associate AV FrameQueue
            //
            // Will return in the frames in PTS order
            rc = avcodec_receive_frame(av_ctxt, queue_f.get());
            if(rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
                // No available, frame, retry again
                break;
            } else if(rc != 0){
                av_log(nullptr, AV_LOG_WARNING, "Failed to receive frame: rc=%d", rc);
                break;
            }

            // Queue need its own reference of the packet
            vs->queue_frame(p_pkt->stream_index, av_frame_clone(queue_f.get()));
        }
    }
}

void VideoState::queue_frame(int stream_idx, AVFrame *f) {
    if(stream_idx == audio_stream_idx) {
        a_queue.push(f);
    } else if(stream_idx == video_stream_idx) {
        v_queue.push(f);
    }
}

AVFrame* VideoState::dequeue_frame(int stream_idx) {
    if(stream_idx == audio_stream_idx) {
        return a_queue.pop();
    } else if(stream_idx == video_stream_idx) {
        return v_queue.pop();
    }

    return nullptr;
}

void VideoState::check_sdl(const std::string& action, int line) {
    const std::string error {SDL_GetError()};
    if(!error.empty()) {
        std::cerr << action + " | SDL line " + std::to_string(line) + " | " + error << std::endl;
        std::exit(-1);
    }
}

void VideoState::check_av(const std::string& msg, int rc, int line) {
    if( rc < 0 ) {
        char errbuf[128];
        av_strerror(rc, errbuf, sizeof(errbuf));
        std::cerr << msg + " | AV line " + std::to_string(line) + " rc=" + std::to_string(rc) + " | " + errbuf << std::endl;
        std::exit(-1);
    }
}

