#pragma once

#if defined (_WIN32) && defined (DLL_EXPORT)
# define LIBRTSP_API __declspec(dllexport)
#elif defined (__GNUC__) && (__GNUC__ >= 4)
# define LIBRTSP_API __attribute__((visibility("default")))
#else
# define LIBRTSP_API
#endif

#ifdef __LIBPV4__
/* Avoid unhelpful warnings from libvlc with our deprecated APIs */
#   define LIBPV4_DEPRECATED
#elif defined(__GNUC__) && \
      (__GNUC__ > 3 || __GNUC__ == 3 && __GNUC_MINOR__ > 0)
# define LIBPV4_DEPRECATED __attribute__((deprecated))
#else
# define LIBRTSP_DEPRECATED
#endif

#include <stdint.h>

# ifdef __cplusplus
extern "C" {
# endif

struct librtsp_t;

#define LIBRTSP_OK			(0)
#define LIBRTSP_TIMEOUT	    (1)
#define LIBRTSP_EOF		    (2)
#define LIBRTSP_ERR		    (3)
#define LIBRTSP_USR_STOP	(4)
#define LIBRTSP_NOT_FOUND	(5)
#define LIBRTSP_AUTH_ERR	(6)

typedef int status_t;

typedef struct librtsp_media_format_t {
    const char* type;		/*"audio" "video" "text"*/
    const char* codec;
    const char* extra;		/*sps pps*/

    uint32_t i_bitrate = 0;

    int     b_packetized;
    struct {
        int i_rate = 0;
        int i_channels = 0;
        int i_bitspersample = 0;
    } audio;

    struct {
        int i_width = 0;
        int i_height = 0;
    } video;

    struct
    {
        char* psz_language;
    }text;
}librtsp_media_format_t;

typedef struct librtsp_track_t {
        const char*               name;
        bool                      b_muxed;
        bool                      b_quicktime;
        bool                      b_asf;
        librtsp_media_format_t    fmt;
}librtsp_track_t;

LIBRTSP_API librtsp_t *librtsp_new();
LIBRTSP_API void       librtsp_release(librtsp_t *p_rtsp);

LIBRTSP_API void       librtsp_toggle_pause(librtsp_t*);
LIBRTSP_API int        librtsp_seek(librtsp_t*,double f_time); /* in second */

LIBRTSP_API int64_t    librtsp_get_currenttime(librtsp_t*);
LIBRTSP_API int64_t    librtsp_get_starttime(librtsp_t*);
LIBRTSP_API void       librtsp_set_usetcp(librtsp_t*, bool bUseTcp);
LIBRTSP_API void       librtsp_set_auth(librtsp_t*, const char* user_name, const char* password);
LIBRTSP_API void       librtsp_set_useragent(librtsp_t*, const char* user_agent);

//开始播放,阻塞操作
LIBRTSP_API status_t   librtsp_start(librtsp_t*, const char* Uri);
LIBRTSP_API void       librtsp_stop(librtsp_t*);
//流信息
LIBRTSP_API void       librtsp_on_track(librtsp_track_t* track) {}

typedef void(*librtsp_trackcallback_t)(const struct librtsp_track_t *, void *data);

typedef void(*librtsp_datacallback_t)(librtsp_track_t* track,
    uint8_t* p_buffer,
    int i_size,
    int i_truncated_bytes,
    int64_t pts,
    int64_t dts,
    void *data);

LIBRTSP_API void       librtsp_track_set_callbacks(librtsp_t*,
                        librtsp_trackcallback_t on_track,
                        void *opaque);

LIBRTSP_API void       librtsp_data_set_callbacks(librtsp_t*,
                        librtsp_datacallback_t on_data,
                        void *opaque);
# ifdef __cplusplus
}
# endif