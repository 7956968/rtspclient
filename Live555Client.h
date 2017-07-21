
#ifndef LIVE555_CLIENT_H__
#define LIVE555_CLIENT_H__

#include <stdint.h>
#include <string>
#include <vector>


#define RTSP_OK			(0)
#define RTSP_TIMEOUT	(1)
#define RTSP_EOF		(2)
#define RTSP_ERR		(3)
#define RTSP_USR_STOP	(4)
#define RTSP_NOT_FOUND	(5)
#define RTSP_AUTH_ERR	(6)

class MediaSession;
class MyRTSPClient;

 class Live555Client
{
    friend class MyRTSPClient;
public:
    class LiveTrack {
    public:
        struct media_format {

            std::string	type;		/*"audio" "video" "text"*/
            std::string codec;
            std::string extra;		/*sps pps*/

            uint32_t i_bitrate = 0;

            int     b_packetized;
            struct {
                int i_rate          = 0;
                int i_channels      = 0;
                int i_bitspersample = 0;
            } audio;

            struct {
                int i_width  = 0;
                int i_height = 0;
            } video;

            struct  
            {
                std::string psz_language;
            }text;
        };
    public:
        Live555Client* parent;
        void* media_sub_session;

        bool            b_muxed;
        bool            b_quicktime;
        bool            b_asf;

        bool            b_discard_trunc;

        uint8_t         *p_buffer;
        unsigned int    i_buffer;

        bool            b_rtcp_sync;
        char            waiting;
        int64_t         i_pts;
        double          f_npt;

        media_format fmt;

    public:
        LiveTrack(Live555Client* p_sys, void* sub, int buffer_size);
        virtual ~LiveTrack();
        int init();

        bool isWaiting() { return waiting != 0; }
        void doWaiting(char val) { waiting = val; }
        bool isMuxed() { return b_muxed; }
        bool isQuicktime() { return b_quicktime; }
        bool isAsf() { return b_asf; }
        bool discardTruncated() { return b_discard_trunc; }
        void* getMediaSubsession() { return media_sub_session; }
        std::string getSessionId() const;
        std::string getSessionName() const;
        void setNPT(double npt) { f_npt = npt; }
        double getNPT() { return f_npt; }

        media_format& getFormat() { return fmt; }

        uint8_t* buffer() { return p_buffer; }
        unsigned int buffer_size() { return i_buffer; }
        static void streamRead(void *opaque, unsigned int i_size,
                        unsigned int i_truncated_bytes, struct timeval pts,
                        unsigned int duration );
        static void streamClose(void *opaque );
        virtual void onDebug(const char* msg) {}
    };
private:
    void* env;
    void* scheduler;
    MyRTSPClient* rtsp;
    MediaSession* m_pMediaSession;
    void* taskKeepAlive;

    char  event_rtsp;
    char  event_data;

    bool  b_get_param;

    int				 live555ResultCode;/* live555返回的http状态值 */
    unsigned int     i_timeout;     /* session timeout value in seconds */

    int64_t          i_pcr; /* The clock */
    double           f_seekTime;
    double           f_npt;
    double           f_npt_length;
    double           f_npt_start;

    bool             b_paused;
    bool             b_multicast;
    int              i_no_data_ti;  /* consecutive number of TaskInterrupt */

    bool             b_rtsp_tcp;
    bool             b_is_paused;
    volatile bool    b_do_control_pause_state;

    unsigned short   u_port_begin;  /* RTP port that client will be use */

    std::string user_agent;
    std::string user_name;
    std::string password;
    std::vector<LiveTrack*> listTracks;

    volatile bool demuxLoopFlag;

    static void taskInterruptData( void *opaque );
    static void taskInterruptRTSP( void *opaque );
    static void taskInterrupKeepAlive(void *opaque);

    int waitLive555Response( int i_timeout = 0 /* ms */ );

    void controlPauseState();
    void controlSeek();

    int demux(void);
    int demux_loop();

    void onStreamRead(LiveTrack* track, unsigned int i_size,
        unsigned int i_truncated_bytes, struct timeval pts,
        unsigned int duration);
    // callback functions
    void continueAfterDESCRIBE(int result_code, char* result_string);
    void continueAfterOPTIONS(int result_code, char* result_string);
    void live555Callback(int result_code);
    void onStreamClose(LiveTrack* track);

public:
    Live555Client(void);
    virtual ~Live555Client(void);

    void togglePause();
    int seek(double f_time /* in second */);

    int64_t getCurrentTime() { return (int64_t)(f_npt * 1000000.0); }
    int64_t getStartTime() { return (int64_t)(f_npt_start * 1000000.0); }

    bool isPaused() const { return b_is_paused; }

    void setUseTcp(bool bUseTcp) { b_rtsp_tcp = bUseTcp; }
    void setUser(const char* user_name, const char* password);
    void setUserAgent(const char* user_agent) { user_agent = user_agent; }
    /* double ports ,like 7000-7001, another for rtcp*/
    void setDestination(std::string Addr, int DstPort);
    void clearDestination() { setDestination("", 0); }

    void setRTPPortBegin(unsigned short port_begin) { u_port_begin = port_begin; }
    unsigned short getRTPPortNoUse() { return u_port_begin; }

protected:
    //开始播放,阻塞操作
    int PlayRtsp(std::string Uri);
    virtual void StopRtsp();
    //流信息
    virtual void onInitializedTrack(LiveTrack* track) {}

    //一帧数据
    virtual void onData(LiveTrack* track, uint8_t* p_buffer, int i_size, int i_truncated_bytes, int64_t pts, int64_t dts) {}
    virtual void onResetPcr() {}

    virtual void onDebug(const char* msg) {}
};

#endif//LIVE555_CLIENT_H__