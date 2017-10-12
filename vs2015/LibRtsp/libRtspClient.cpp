#include "libRtspClient.h"

#include <atomic>
#include <thread>

#include "../../Live555Client.h"

void LiveTrackToTrack_t(Live555Client::LiveTrack* src, librtsp_track_t* dst)
{
    memset(dst, 0 ,sizeof(librtsp_track_t));

    dst->name = src->getSessionName();

    dst->b_asf = src->b_asf;
    dst->fmt.type = src->fmt.type.c_str();
    dst->fmt.codec = src->fmt.codec.c_str();
    dst->fmt.extra = src->fmt.codec.c_str();
    dst->fmt.b_packetized = src->fmt.b_packetized;

    dst->fmt.audio.i_bitspersample = src->fmt.audio.i_bitspersample;
    dst->fmt.audio.i_rate = src->fmt.audio.i_rate;
    dst->fmt.audio.i_channels = src->fmt.audio.i_channels;

    dst->fmt.video.i_height = src->fmt.video.i_height;
    dst->fmt.video.i_width = src->fmt.video.i_width;
}

struct librtsp_t : public Live555Client
{
    virtual void onInitializedTrack(LiveTrack* track)
    {
        librtsp_track_t track_t;
        LiveTrackToTrack_t(track, &track_t);
        fTrack(&track_t, VarTrack);
    }

    virtual void onData(LiveTrack* track,
        uint8_t* p_buffer,
        int i_size,
        int i_truncated_bytes,
        int64_t pts, int64_t dts)
    {
        librtsp_track_t track_t;
        LiveTrackToTrack_t(track, &track_t);

        fData(&track_t, p_buffer, i_size, i_truncated_bytes, pts, dts, VarData);
    }
    int start(const char* Uri)
    {
        int r = 0;
        bRuning = 1;
        r = PlayRtsp(Uri);
        bRuning = 0;
        return r;
    }

    void stop()
    {
        StopRtsp();
        while (bRuning) {
            std::this_thread::sleep_for(std::chrono::microseconds(2));
        }
    }

    std::atomic<int> bRuning = 0;

    librtsp_trackcallback_t fTrack;
    void* VarTrack;

    librtsp_datacallback_t fData;
    void* VarData;
};

librtsp_t *librtsp_new()
{
    return new librtsp_t;
}

void librtsp_release(librtsp_t *p_rtsp)
{
    if (p_rtsp) delete p_rtsp;
}

void librtsp_toggle_pause(librtsp_t* p_rtsp)
{
    if (p_rtsp) p_rtsp->togglePause();
}

int librtsp_seek(librtsp_t* p_rtsp, double f_time)
{
    if (p_rtsp) {
        return p_rtsp->seek(f_time);
    }
    return 0;
}

int64_t librtsp_get_currenttime(librtsp_t* p_rtsp)
{
    if (p_rtsp) {
        p_rtsp->getCurrentTime();
    }
    return 0;
}

int64_t librtsp_get_starttime(librtsp_t* p_rtsp)
{
    return p_rtsp->getStartTime();
}

void librtsp_set_usetcp(librtsp_t* p_rtsp, bool bUseTcp)
{
    if (p_rtsp) p_rtsp->setUseTcp(bUseTcp);
}

void librtsp_set_auth(librtsp_t* p_rtsp, const char* user_name, const char* password)
{
    if (p_rtsp) p_rtsp->setUser(user_name, password);
}

void librtsp_set_useragent(librtsp_t* p_rtsp, const char* user_agent)
{
    if (p_rtsp) p_rtsp->setUserAgent(user_agent);
}
//开始播放,阻塞操作
status_t librtsp_start(librtsp_t* p_rtsp, const char* Uri)
{
    if (p_rtsp) {
        return p_rtsp->start(Uri);
    }
    return LIBRTSP_OK;
}

void librtsp_stop(librtsp_t* p_rtsp)
{
    if (p_rtsp) p_rtsp->stop();
}

void librtsp_track_set_callbacks(librtsp_t* p_rtsp,
    librtsp_trackcallback_t on_track,
    void *opaque)
{
    if (!p_rtsp) return;

    p_rtsp->fTrack = on_track;
    p_rtsp->VarTrack = opaque;
}

void librtsp_data_set_callbacks(librtsp_t* p_rtsp,
        librtsp_datacallback_t on_data,
        void *opaque)
{
    if (!p_rtsp) return;

    p_rtsp->fData = on_data;
    p_rtsp->VarData = opaque;
}
