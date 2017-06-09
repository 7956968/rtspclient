
#include "live555client.h"
#include <UsageEnvironment.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <liveMedia.hh>
#include <liveMedia_version.hh>
#include <Base64.hh>
#include <RTSPCommon.hh>
#include <chrono>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <sstream>
#include <iostream>

#if defined(_WIN32) || defined(WIN32)
#pragma warning(disable:4996)
#endif

using namespace std;

#define DEFAULT_WAIT_TIME         (200)         //默认超时时间,200毫秒

#define HTTP_OK	                  (0)
//这个错误号表明虽然live555没返回http错误。但我们处理RTSP过程中碰到了错误
#define HTTP_ERR_USR	          (99)
#define HTTP_TIMEOUT        	  (180)
#define HTTP_ERR_EOF	          (502)
#define HTTP_STREAM_NOT_FOUND	  (404)
#define HTTP_SESSION_NOT_FOUND	  (454)
#define HTTP_UNSUPPORTED_TRANSPOR (461)

/* All timestamp below or equal to this define are invalid/unset
 * XXX the numerical value is 0 because of historical reason and will change.*/
#define VLC_TS_INVALID INT64_C(0)
#define VLC_TS_0 INT64_C(1)

#define CLOCK_FREQ INT64_C(1000000)

unsigned char* parseH264ConfigStr(char const* configStr,
	unsigned int& configSize);
uint8_t * parseVorbisConfigStr(char const* configStr,
	unsigned int& configSize);

static /* Base64 decoding */
size_t vlc_b64_decode_binary_to_buffer(uint8_t *p_dst, size_t i_dst, const char *p_src)
{
	static const int b64[256] = {
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 00-0F */
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 10-1F */
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,  /* 20-2F */
		52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  /* 30-3F */
		-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  /* 40-4F */
		15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,  /* 50-5F */
		-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  /* 60-6F */
		41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  /* 70-7F */
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 80-8F */
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 90-9F */
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* A0-AF */
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* B0-BF */
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* C0-CF */
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* D0-DF */
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* E0-EF */
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1   /* F0-FF */
	};
	uint8_t *p_start = p_dst;
	uint8_t *p = (uint8_t *)p_src;

	int i_level;
	int i_last;

	for (i_level = 0, i_last = 0; (size_t)(p_dst - p_start) < i_dst && *p != '\0'; p++)
	{
		const int c = b64[(unsigned int)*p];
		if (c == -1)
			break;

		switch (i_level)
		{
		case 0:
			i_level++;
			break;
		case 1:
			*p_dst++ = (i_last << 2) | ((c >> 4) & 0x03);
			i_level++;
			break;
		case 2:
			*p_dst++ = ((i_last << 4) & 0xf0) | ((c >> 2) & 0x0f);
			i_level++;
			break;
		case 3:
			*p_dst++ = ((i_last & 0x03) << 6) | c;
			i_level = 0;
		}
		i_last = c;
	}

	return p_dst - p_start;
}


static inline Boolean toBool(bool b) { return b ? True : False; } // silly, no?

class MyRTSPClient : public RTSPClient
{
protected:
    Live555Client* parent;
    Boolean fSupportsGetParameter;
	string destination;
	int destPort = 0;

	PresentationTimeSessionNormalizer* PtsSessionNormalizer = nullptr;
public:
    MyRTSPClient( UsageEnvironment& env, char const* rtspURL, int verbosityLevel,
                   char const* applicationName, portNumBits tunnelOverHTTPPortNum,
                   Live555Client *p_sys) 
        : RTSPClient( env, rtspURL, verbosityLevel, applicationName,
                   tunnelOverHTTPPortNum
#if LIVEMEDIA_LIBRARY_VERSION_INT >= 1373932800
                   , -1
#endif
                   )
                   , parent (p_sys)
                   , fSupportsGetParameter(False)
				   , PtsSessionNormalizer(new PresentationTimeSessionNormalizer(env))
    {
    }
	~MyRTSPClient()
	{
		if (PtsSessionNormalizer) {
			Medium::close(PtsSessionNormalizer);
		}
	}

	virtual Boolean setRequestFields(RequestRecord* request,
		char*& cmdURL, Boolean& cmdURLWasAllocated,
		char const*& protocolStr,
		char*& extraHeaders, Boolean& extraHeadersWereAllocated);

	void setDestination(string Addr, int DstPort);

	PresentationTimeSubsessionNormalizer*
		createNewPresentationTimeSubsessionNormalizer(FramedSource* inputSource, 
			RTPSource* rtpSource, 
			char const* codecName);
	static void continueAfterDESCRIBE(RTSPClient* client, int result_code, char* result_string);
	static void continueAfterOPTIONS(RTSPClient* client, int result_code, char* result_string);
	static void default_live555_callback(RTSPClient* client, int result_code, char* result_string);

    void setSupportsGetParameter(Boolean val) { fSupportsGetParameter = val; }
    Boolean isSupportsGetParameter() { return fSupportsGetParameter; }
};

void MyRTSPClient::setDestination(string Addr, int DstPort)
{
	destination = Addr;
	destPort = DstPort;
}

PresentationTimeSubsessionNormalizer*
MyRTSPClient::createNewPresentationTimeSubsessionNormalizer(FramedSource* inputSource,
	RTPSource* rtpSource,
	char const* codecName)
{
	if (PtsSessionNormalizer) {
		return PtsSessionNormalizer->createNewPresentationTimeSubsessionNormalizer(inputSource, rtpSource, codecName);
	}

	return nullptr;
}

Boolean MyRTSPClient::setRequestFields(RTSPClient::RequestRecord* request,
	char*& cmdURL, Boolean& cmdURLWasAllocated,
	char const*& protocolStr,
	char*& extraHeaders, Boolean& extraHeadersWereAllocated) {

	if ((destination.size() > 0) && (strcmp(request->commandName(), "SETUP") == 0)) {  //只覆盖SETUP消息,其它消息仍然使用RTSPClient::setRequestFields()
		extraHeaders = new char[256];
		extraHeadersWereAllocated = True;

		Boolean streamUsingTCP = (request->booleanFlags() & 0x1) != 0;
		if (streamUsingTCP) {
			if (strcmp(request->subsession()->protocolName(), "UDP") == 0)
				sprintf(extraHeaders, "Transport: RAW/RAW/UDP;unicast;interleaved=0-1;destination=%s;client_port=%d-%d\r\n",
					destination.c_str(), destPort, destPort + 1);
			else
				sprintf(extraHeaders, "Transport: RTP/AVP;unicast;interleaved=0-1;destination=%s;client_port=%d-%d\r\n",
					destination.c_str(), destPort, destPort + 1);
		}
		else {
			sprintf(extraHeaders, "Transport: RTP/TCP;unicast;interleaved=0-1;destination=%s;client_port=%d-%d\r\n",
				destination.c_str(), destPort, destPort + 1);
		}
		return True;
	}

	return RTSPClient::setRequestFields(request, cmdURL, cmdURLWasAllocated, protocolStr, extraHeaders, extraHeadersWereAllocated);
}

void MyRTSPClient::continueAfterDESCRIBE(RTSPClient* client, int result_code, char* result_string)
{
	MyRTSPClient* pThis = static_cast<MyRTSPClient*>(client);
	pThis->parent->continueAfterDESCRIBE(result_code, result_string);
	delete[] result_string;
}

void MyRTSPClient::continueAfterOPTIONS(RTSPClient* client, int result_code, char* result_string)
{
	MyRTSPClient* pThis = static_cast<MyRTSPClient*>(client);

	Boolean serverSupportsGetParameter = RTSPOptionIsSupported("GET_PARAMETER", result_string);
	pThis->setSupportsGetParameter(serverSupportsGetParameter);
	pThis->parent->continueAfterOPTIONS(result_code, result_string);
	delete[] result_string;
}

void MyRTSPClient::default_live555_callback(RTSPClient* client, int result_code, char* result_string)
{
	MyRTSPClient* pThis = static_cast<MyRTSPClient*>(client);
	delete[]result_string;
	pThis->parent->live555Callback(result_code);
	//这里好好处理一下转换成我们的错误值
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
Live555Client::LiveTrack::LiveTrack(Live555Client* p_sys, void* sub, int buffer_size)
	: parent(p_sys)
	, media_sub_session(sub)
	, i_buffer(buffer_size)
{
	b_quicktime = false;
	b_asf = false;
	b_muxed = false;
	b_discard_trunc = false;

	waiting = 0;
	b_rtcp_sync = false;
	i_pts = VLC_TS_INVALID;
	f_npt = 0.;
}

Live555Client::LiveTrack::~LiveTrack()
{
	if (p_buffer) {
		delete[] p_buffer;
		p_buffer = NULL;
	}
}


int Live555Client::LiveTrack::init()
{
    MediaSubsession* sub = static_cast<MediaSubsession*>(media_sub_session);
    p_buffer    = new uint8_t [i_buffer + 4];

    if (!p_buffer)
        return RTSP_ERR;

	fmt.type  = sub->mediumName();
	fmt.codec = sub->codecName();
	if (fmt.type == "audio") {

		fmt.audio.i_channels = sub->numChannels();
		fmt.audio.i_rate = sub->rtpTimestampFrequency();

		if ((fmt.codec == "MPA") ||
			(fmt.codec == "MPA-ROBUST") ||
			(fmt.codec == "X-MP3-DRAFT-00") ||
			(fmt.codec == "AC3")) {
			fmt.audio.i_rate = 0;
		}
		else if (fmt.codec == "L16"){
			fmt.audio.i_bitspersample = 16;
		}
		else if (fmt.codec == "L20"){
			fmt.audio.i_bitspersample = 20;
		}
		else if (fmt.codec == "L24"){
			fmt.audio.i_bitspersample = 24;
		}
		else if (fmt.codec == "L8"){
			fmt.audio.i_bitspersample = 8;
		}
		else if (fmt.codec == "DAT12"){
			fmt.audio.i_bitspersample = 12;
		}
		else if (fmt.codec == "PCMU"){
			fmt.audio.i_bitspersample = 8;
		}
		else if (fmt.codec == "PCMA"){
			fmt.audio.i_bitspersample = 8;
		}
		else if (fmt.codec == "G726")  {
			fmt.audio.i_rate = 8000;
			fmt.audio.i_channels = 1;
			if (!strcmp(sub->codecName() + 5, "40"))
				fmt.i_bitrate = 40000;
			else if (!strcmp(sub->codecName() + 5, "32"))
				fmt.i_bitrate = 32000;
			else if (!strcmp(sub->codecName() + 5, "24"))
				fmt.i_bitrate = 24000;
			else if (!strcmp(sub->codecName() + 5, "16"))
				fmt.i_bitrate = 16000;
		}
		else if (fmt.codec == "MP4A-LATM") {
			unsigned int i_extra;
			uint8_t      *p_extra;

			if ((p_extra = parseStreamMuxConfigStr(sub->fmtp_config(),
				i_extra)))
			{
				fmt.extra = string((char*)p_extra, i_extra);
				delete[] p_extra;
			}
			/* Because the "faad" decoder does not handle the LATM
			* data length field at the start of each returned LATM
			* frame, tell the RTP source to omit. */
			((MPEG4LATMAudioRTPSource*)sub->rtpSource())->omitLATMDataLengthField();
		}
		else if (fmt.codec == "MPEG4-GENERIC") {
			unsigned int i_extra;
			uint8_t      *p_extra;

			if ((p_extra = parseGeneralConfigStr(sub->fmtp_config(),
				i_extra)))
			{
				fmt.extra = string((char*)p_extra, i_extra);
				delete[] p_extra;
			}
		}
		else if (fmt.codec == "SPEEX") {
			if (fmt.audio.i_rate == 0)
			{
				onDebug("Using 8kHz as default sample rate.");
				fmt.audio.i_rate = 8000;
			}
		}
		else if (fmt.codec == "VORBIS") {
			unsigned int i_extra;
			unsigned char *p_extra;
			if ((p_extra = parseVorbisConfigStr(sub->fmtp_config(),
				i_extra)))
			{
				fmt.extra = string((char*)p_extra, i_extra);
				delete[] p_extra;
			}
		}

	}
	else if (fmt.type == "video") {
		if (fmt.codec == "MPV")
		{
			fmt.b_packetized = false;
		}
		else if (fmt.codec == "H264")
		{
			unsigned int i_extra = 0;
			uint8_t      *p_extra = NULL;

			fmt.b_packetized = false;

			if ((p_extra = parseH264ConfigStr(sub->fmtp_spropparametersets(),
				i_extra)))
			{
				fmt.extra = string((char*)p_extra, i_extra);
				delete[] p_extra;
			}
		}
#if LIVEMEDIA_LIBRARY_VERSION_INT >= 1393372800 // 2014.02.26
		else if (fmt.codec == "H265")
		{
			unsigned int i_extra1 = 0, i_extra2 = 0, i_extra3 = 0, i_extraTot;
			uint8_t      *p_extra1 = NULL, *p_extra2 = NULL, *p_extra3 = NULL;

			fmt.b_packetized = false;

			p_extra1 = parseH264ConfigStr(sub->fmtp_spropvps(), i_extra1);
			p_extra2 = parseH264ConfigStr(sub->fmtp_spropsps(), i_extra2);
			p_extra3 = parseH264ConfigStr(sub->fmtp_sproppps(), i_extra3);
			i_extraTot = i_extra1 + i_extra2 + i_extra3;
			if (i_extraTot > 0)
			{
				if (p_extra1)
				{
					fmt.extra = string((char*)p_extra1, i_extra1);
					delete[] p_extra1;
				}
				if (p_extra2)
				{
					fmt.extra += string((char*)p_extra2, i_extra2);
					delete[] p_extra2;
				}
				if (p_extra3)
				{
					fmt.extra += string((char*)p_extra3, i_extra3);
					delete[] p_extra3;
				}
			}
		}
#endif
		else if (fmt.codec == "MP4V-ES")
		{
			unsigned int i_extra;
			uint8_t      *p_extra;

			if ((p_extra = parseGeneralConfigStr(sub->fmtp_config(),
				i_extra)))
			{
				fmt.extra = string((char*)p_extra, i_extra);
				delete[] p_extra;
			}
		}
		else if ((fmt.codec == "X-QT")        ||
			     (fmt.codec == "X-QUICKTIME") ||
			     (fmt.codec == "X-QDM")       ||
			     (fmt.codec == "X-SV3V-ES")   ||
			     (fmt.codec == "X-SORENSONVIDEO"))
		{
			b_quicktime = true;
		}
		else if (fmt.codec == "DV")
		{
			b_discard_trunc = true;
		}
		else if (fmt.codec == "THEORA")
		{
			unsigned int i_extra;
			uint8_t      *p_extra;

			if ((p_extra = parseVorbisConfigStr(sub->fmtp_config(),
				i_extra)))
			{
				fmt.extra = string((char*)p_extra, i_extra);
				delete[] p_extra;
			}
			else
				onDebug("Missing or unsupported theora header.");
		}

	}
	
    /* Try and parse a=lang: attribute */
    const char* p_lang = strstr( sub->savedSDPLines(), "a=lang:" );
    if( p_lang )
    {
       unsigned i_lang_len;
       p_lang += 7;
       i_lang_len = strcspn( p_lang, " \r\n" );
       fmt.text.psz_language = string( p_lang, i_lang_len );
    }

    if( sub->rtcpInstance() != NULL ){
        sub->rtcpInstance()->setByeHandler( Live555Client::LiveTrack::streamClose, this );
    }

    return RTSP_OK;
}

string Live555Client::LiveTrack::getSessionId() const
{
    MediaSubsession* sub = static_cast<MediaSubsession*>(media_sub_session);
    if (!sub)
        return "";

    return sub->sessionId();
}

string Live555Client::LiveTrack::getSessionName() const
{
    MediaSubsession* sub = static_cast<MediaSubsession*>(media_sub_session);
    if (!sub)
        return "";

    return sub->controlPath();
}


void Live555Client::LiveTrack::streamRead(void *opaque, unsigned int i_size,
                        unsigned int i_truncated_bytes, struct timeval pts,
                        unsigned int duration)
{
    Live555Client::LiveTrack* pThis = static_cast<Live555Client::LiveTrack*>(opaque);
    pThis->parent->onStreamRead(pThis, i_size, i_truncated_bytes, pts, duration);
}

void Live555Client::LiveTrack::streamClose(void* opaque)
{
    Live555Client::LiveTrack* pThis = static_cast<Live555Client::LiveTrack*>(opaque);
    pThis->parent->onStreamClose(pThis);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

void Live555Client::taskInterruptData( void *opaque )
{
    Live555Client *pThis = static_cast<Live555Client*>(opaque);

    pThis->i_no_data_ti++;

    /* Avoid lock */
    pThis->event_data = (char)0xff;
}

void Live555Client::taskInterruptRTSP( void *opaque )
{
    Live555Client *pThis = static_cast<Live555Client*>(opaque);

    pThis->live555ResultCode = HTTP_TIMEOUT;
    /* Avoid lock */
    pThis->event_rtsp = (char)0xff;
}

bool Live555Client::waitLive555Response( int i_timeout /* ms */ )
{
    TaskToken task = nullptr;
    BasicTaskScheduler* sch = (BasicTaskScheduler*)scheduler;
    event_rtsp = 0;
    if( i_timeout > 0 )
    {
        /* Create a task that will be called if we wait more than timeout ms */
        task = sch->scheduleDelayedTask( i_timeout*1000,
                                                      taskInterruptRTSP,
                                                      this );
    }
    event_rtsp = 0;
	live555ResultCode = HTTP_OK;
    sch->doEventLoop( &event_rtsp );
    //here, if b_error is true and i_live555_ret = 0 we didn't receive a response
    if(task)
    {
        /* remove the task */
        sch->unscheduleDelayedTask( task );
    }
    return !live555ResultCode;
}

#define DEFAULT_FRAME_BUFFER_SIZE 500000

//这个接口到底危险不危险?
void Live555Client::controlPauseState()
{
    RTSPClient* client = static_cast<RTSPClient*>(rtsp);

    b_is_paused = !b_is_paused;

    if (b_is_paused) {
        client->sendPauseCommand( *m_pMediaSession, MyRTSPClient::default_live555_callback );
    }
    else {
        client->sendPlayCommand( *m_pMediaSession, MyRTSPClient::default_live555_callback, -1.0f, -1.0f, m_pMediaSession->scale() );
    }

    waitLive555Response(DEFAULT_WAIT_TIME);
}

void Live555Client::controlSeek()
{
    RTSPClient* client = static_cast<RTSPClient*>(rtsp);

    client->sendPauseCommand( *m_pMediaSession, MyRTSPClient::default_live555_callback );
    waitLive555Response(DEFAULT_WAIT_TIME);
    
    client->sendPlayCommand( *m_pMediaSession, MyRTSPClient::default_live555_callback, f_seekTime, -1.0f, m_pMediaSession->scale() );
    waitLive555Response(DEFAULT_WAIT_TIME);

    f_seekTime = -1.0;
    /* Retrieve the starttime if possible */
    f_npt = f_npt_start = m_pMediaSession->playStartTime();

    /* Retrieve the duration if possible */
    if(m_pMediaSession->playEndTime() > 0 )
        f_npt_length = m_pMediaSession->playEndTime();
}

int Live555Client::demux(void)
{
    TaskToken      task;
    MyRTSPClient*    client = static_cast<MyRTSPClient*>(rtsp);
    TaskScheduler* sch = static_cast<TaskScheduler*>(scheduler);

    bool    b_send_pcr = true;

    /* Check if we need to send the server a Keep-A-Live signal */
    if( b_timeout_call && client && m_pMediaSession)
    {
        char *psz_bye = NULL;
        if (client->isSupportsGetParameter())
            client->sendGetParameterCommand( *m_pMediaSession, NULL, psz_bye );
        else {
            Authenticator authenticator;
            authenticator.setUsernameAndPassword( user_name.c_str(), password.c_str() );
            client->sendOptionsCommand(NULL, &authenticator);
        }

        b_timeout_call = false;
    }

    if (b_is_paused)
        return RTSP_OK;

    /* First warn we want to read data */
    event_data = 0;

    for (auto it = listTracks.begin(); it != listTracks.end(); ++it)
    {
        LiveTrack *tk = *it;

        MediaSubsession* sub = static_cast<MediaSubsession*>(tk->getMediaSubsession());
        uint8_t* p_buffer = tk->buffer();

        if( tk->getFormat().codec == "AMR" ||
            tk->getFormat().codec == "AMR-WB" )
        {
            p_buffer++;
        }
        else if( tk->getFormat().codec == "H261" || 
			     tk->getFormat().codec == "H264" || 
			     tk->getFormat().codec == "H265" )
        {
            p_buffer += 4;
        }

        if( !tk->isWaiting() )
        {
            tk->doWaiting(1);
            sub->readSource()->getNextFrame( p_buffer, tk->buffer_size(),
                                          Live555Client::LiveTrack::streamRead, tk, Live555Client::LiveTrack::streamClose, tk );
        }
    }

    /* Create a task that will be called if we wait more than 300ms */
    task = sch->scheduleDelayedTask( 300000, taskInterruptData, this );

    /* Do the read */
    sch->doEventLoop( &event_data );

    /* remove the task */
    sch->unscheduleDelayedTask( task );

    /* Check for gap in pts value */
     for (auto it = listTracks.begin(); it != listTracks.end(); ++it)
     {
		LiveTrack *tk = *it;
		MediaSubsession* sub = static_cast<MediaSubsession*>(tk->getMediaSubsession());

		if( !tk->b_muxed && !tk->b_rtcp_sync && sub->rtpSource() && sub->rtpSource()->hasBeenSynchronizedUsingRTCP() )
		{
             onDebug("tk->rtpSource->hasBeenSynchronizedUsingRTCP()" );

             onResetPcr();
             tk->b_rtcp_sync = true;
             /* reset PCR */
             tk->i_pts = VLC_TS_INVALID;
             tk->f_npt = 0.;
             i_pcr = 0;
             f_npt = 0.;
         }
     }

    if( b_multicast && b_no_data &&
     ( i_no_data_ti > 120 ) )
    {
     onDebug( "no multicast data received in 36s, aborting" );
     return RTSP_TIMEOUT;
    }
    else if( !b_multicast && !b_paused &&
           b_no_data && ( i_no_data_ti > 34 ) )
    {
     onDebug( "no data received in 10s, aborting" );
     return RTSP_TIMEOUT;
    }

    if( i_no_data_ti > 33 || live555ResultCode == HTTP_SESSION_NOT_FOUND) { //no data received in 10s, eof ?
        return RTSP_TIMEOUT;
    }

    return RTSP_OK;
}

int Live555Client::demux_loop()
{
    std::chrono::high_resolution_clock::time_point last_call_timeout= std::chrono::high_resolution_clock::now();

    while (demuxLoopFlag)
    {
        std::chrono::seconds lasting = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - last_call_timeout);
        if (lasting.count() >= (i_timeout - 2)) {
            last_call_timeout= std::chrono::high_resolution_clock::now();
            b_timeout_call = true;
        }

        if (b_do_control_pause_state) {
            if (f_seekTime >= 0)
                controlSeek();
            else
                controlPauseState();

            b_do_control_pause_state = false;
        }

        int r = demux();
        if (r != RTSP_OK){
			return r;
        }
		if (live555ResultCode != 0) {
			r = RTSP_ERR;

			if (live555ResultCode == HTTP_ERR_EOF) {
				r = RTSP_EOF;
			}
			return r;
		}
    }

	//用户主动关闭
	return RTSP_USR_STOP;
}

Live555Client::Live555Client(void)
    : env(NULL)
    , scheduler(NULL)
    , rtsp(NULL)
    , m_pMediaSession(NULL)
    , event_rtsp(0)
    , event_data(0)
    , b_get_param(false)
    , live555ResultCode(0)
    , i_timeout(60)
    , b_timeout_call(false)
    , i_pcr(VLC_TS_0)
    , f_seekTime(-1.0)
    , f_npt(0)
    , f_npt_length(0)
    , f_npt_start(0)
    , b_no_data(false)
    , i_no_data_ti(0)
    , b_is_paused(false)
    , b_do_control_pause_state(false)
    , u_port_begin(0)
    , user_agent("RTSPClient/1.0")
    , demuxLoopFlag(false)
    , b_rtsp_tcp(true)
{
	scheduler = BasicTaskScheduler::createNew();
	env = BasicUsageEnvironment::createNew(*(TaskScheduler*)scheduler);
}

Live555Client::~Live555Client(void)
{
	StopRtsp();

	UsageEnvironment* environment = static_cast<UsageEnvironment*>(env);
	TaskScheduler* sch = static_cast<TaskScheduler*>(scheduler);
	environment->reclaim();
	delete sch;
}

int Live555Client::PlayRtsp(string Uri)
{
	if (Uri.length() == 0) {
		return RTSP_ERR;
	}

	int Status = RTSP_OK;

	Authenticator authenticator;
	UsageEnvironment* environment = static_cast<UsageEnvironment*>(env);
	TaskScheduler* sch = static_cast<TaskScheduler*>(scheduler);

	rtsp = new MyRTSPClient(*environment, Uri.c_str(), 0, user_agent.c_str(), 0, this);
	if (!rtsp) {
		return RTSP_ERR;
	}

	if (user_name.length() != 0 && password.length() != 0) {
		authenticator.setUsernameAndPassword(user_name.c_str(), password.c_str());
	}
	
	rtsp->sendOptionsCommand(&MyRTSPClient::continueAfterOPTIONS, &authenticator);
	if (!waitLive555Response(3000)){
        Status = HttpErrToRtspErr(live555ResultCode);
        goto err;
	}

	f_npt_start = 0;

	/* The PLAY */
	rtsp->sendPlayCommand(*m_pMediaSession, MyRTSPClient::default_live555_callback, f_npt_start, -1, 1);

	if (!waitLive555Response(3000)){
        Status = HttpErrToRtspErr(live555ResultCode);
        goto err;
	}

	/* Retrieve the timeout value and set up a timeout prevention thread */
	i_timeout = rtsp->sessionTimeoutParameter();
	if (i_timeout <= 0)
		i_timeout = 60; /* default value from RFC2326 */
	i_pcr = 0;

	/* Retrieve the starttime if possible */
	f_npt_start = m_pMediaSession->playStartTime();
	if (m_pMediaSession->playEndTime() > 0)
		f_npt_length = m_pMediaSession->playEndTime();

	// now create thread for get data
	b_is_paused = false;
	b_do_control_pause_state = false;
	b_timeout_call = true;
	demuxLoopFlag = true;

	int r = demux_loop();

	if (rtsp && m_pMediaSession)
		rtsp->sendTeardownCommand(*m_pMediaSession, NULL);

	if (m_pMediaSession) {
		Medium::close(m_pMediaSession);
		m_pMediaSession = NULL;
	}

	if (rtsp) {
		RTSPClient::close(rtsp);
		rtsp = NULL;
	}

	for (size_t i = 0; i < listTracks.size(); i++) {
		delete listTracks[i];
	}

	listTracks.clear();

	u_port_begin = 0;
	
	return r;
err:
	if (rtsp) {
		RTSPClient::close(rtsp);
		rtsp = nullptr;
	}

	return Status;
}

void Live555Client::togglePause()
{
    b_do_control_pause_state = true;
}

int Live555Client::seek(double f_time)
{
    if (f_npt_length <= 0)
        return -1; // unsupported

    if (f_time <= 0)
        f_time = 0;

    if (f_time > f_npt_length)
        f_time = f_npt_length;

    f_seekTime = f_time;

    b_do_control_pause_state = true;
    return 0;
}

int Live555Client::StopRtsp()
{
	demuxLoopFlag = false;

    return 0;
}

void Live555Client::setUser(const char* user_name, const char* password)
{
    this->user_name = user_name;
    this->password = password;
}

void Live555Client::setDestination(string Addr, int DstPort)
{
	MyRTSPClient* client = static_cast<MyRTSPClient*>(rtsp);
	client->setDestination(Addr, DstPort);
}

void Live555Client::continueAfterDESCRIBE( int result_code, char* sdp)
{
	live555ResultCode = result_code;
    if ( result_code != 0  || (sdp == nullptr)){
        return;
    }

    MediaSubsessionIterator *iter = NULL;
    MediaSubsession         *sub = NULL;
    MyRTSPClient* client = static_cast<MyRTSPClient*>(rtsp);
    UsageEnvironment* environment = static_cast<UsageEnvironment*>(env);

    int            i_client_port;
    int            i_return = 0;
    unsigned int   i_receive_buffer = 0;
    int            i_frame_buffer = DEFAULT_FRAME_BUFFER_SIZE;
    unsigned const thresh = 200000; /* RTP reorder threshold .2 second (default .1) */

    i_client_port = u_port_begin; //var_InheritInteger( p_demux, "rtp-client-port" );

                                  /* here print sdp on debug */
    printf("SDP content:\n%s", sdp);
    /* Create the session from the SDP */
    m_pMediaSession = MediaSession::createNew(*environment, sdp);

    iter = new MediaSubsessionIterator(*m_pMediaSession);
    while ((sub = iter->next()) != NULL)
    {
        bool b_init;
        LiveTrack* tk;
        /* Value taken from mplayer */
        if (!strcmp(sub->mediumName(), "audio"))
            i_receive_buffer = 200000;
        else if (!strcmp(sub->mediumName(), "video"))
            i_receive_buffer = 2000000;
        else if (!strcmp(sub->mediumName(), "text"))
            ;
        else continue;

        if (i_client_port != -1)
        {
            sub->setClientPortNum(i_client_port);
            i_client_port += 2;
        }

        if (!strcmp(sub->codecName(), "X-ASF-PF"))
            b_init = sub->initiate(0);
        else
            b_init = sub->initiate();

        if (b_init)
        {
            FramedFilter* normalizerFilter = client->createNewPresentationTimeSubsessionNormalizer(
                sub->readSource(), sub->rtpSource(),
                sub->codecName());

            sub->addFilter(normalizerFilter);

            if (sub->rtpSource() != NULL)
            {
                int fd = sub->rtpSource()->RTPgs()->socketNum();

                /* Increase the buffer size */
                if (i_receive_buffer > 0)
                    increaseReceiveBufferTo(*environment, fd, i_receive_buffer);

                /* Increase the RTP reorder timebuffer just a bit */
                sub->rtpSource()->setPacketReorderingThresholdTime(thresh);
            }

            /* Issue the SETUP */
            if (client)
            {
                client->sendSetupCommand(*sub, MyRTSPClient::default_live555_callback, False,
                    toBool(b_rtsp_tcp),
                    False/*toBool( p_sys->b_force_mcast && !b_rtsp_tcp )*/);
                if (!waitLive555Response(200))
                {
                    /* if we get an unsupported transport error, toggle TCP
                    * use and try again */
                    if (live555ResultCode != HTTP_UNSUPPORTED_TRANSPOR) {
                        break;
                    }

                    live555ResultCode = HTTP_OK;
                    client->sendSetupCommand(*sub, MyRTSPClient::default_live555_callback, False,
                        !toBool(b_rtsp_tcp), False);

                    if (!waitLive555Response(200)){
                        onDebug("SETUP failed!");
                        break;
                    }
                    else{
                        b_rtsp_tcp = true;
                    }
                }
            }

            /* Check if we will receive data from this subsession for
            * this track */
            if (sub->readSource() == NULL) continue;
            if (!b_multicast){
                /* We need different rollover behaviour for multicast */
                b_multicast = IsMulticastAddress(sub->connectionEndpointAddress());
            }

            tk = new LiveTrack(this, sub, i_frame_buffer);

            if (tk->init() != RTSP_OK){
                delete tk;
                break;
            }
            else {
                listTracks.push_back(tk);
                onInitializedTrack(tk);
            }
        }
    }
    delete iter;

    if (!listTracks.size()) live555ResultCode = HTTP_ERR_USR;

    /* Retrieve the starttime if possible */
    f_npt_start = m_pMediaSession->playStartTime();

    /* Retrieve the duration if possible */
    if (m_pMediaSession->playEndTime() > 0)
        f_npt_length = m_pMediaSession->playEndTime();

    /* */
    //msg_Dbg( p_demux, "setup start: %f stop:%f", p_sys->f_npt_start, p_sys->f_npt_length );

    /* */
    b_no_data = true;
    i_no_data_ti = 0;

    u_port_begin = i_client_port;

	event_rtsp = 1;

}

void Live555Client::continueAfterOPTIONS( int result_code, char* result_string )
{
    b_get_param =
      // If OPTIONS fails, assume GET_PARAMETER is not supported but
      // still continue on with the stream.  Some servers (foscam)
      // return 501/not implemented for OPTIONS.
      result_code == 0
      && result_string != NULL
      && strstr( result_string, "GET_PARAMETER" ) != NULL;

    RTSPClient* client = static_cast<RTSPClient*>(rtsp);
    client->sendDescribeCommand( MyRTSPClient::continueAfterDESCRIBE );
}

void Live555Client::live555Callback( int result_code )
{
	live555ResultCode = result_code;
    event_rtsp = 1;
}

void Live555Client::onStreamRead(LiveTrack* track, unsigned int i_size,
                        unsigned int i_truncated_bytes, struct timeval pts,
                        unsigned int duration )
{
    MediaSubsession* sub = static_cast<MediaSubsession*>(track->getMediaSubsession());

    int64_t i_pts = (int64_t)pts.tv_sec * INT64_C(1000000) +
        (int64_t)pts.tv_usec;

    int64_t i_dts;

    /* XXX Beurk beurk beurk Avoid having negative value XXX */
    i_pts &= INT64_C(0x00ffffffffffffff);

    /* Retrieve NPT for this pts */
    track->setNPT(sub->getNormalPlayTime(pts));

    if( track->isQuicktime() /* && tk->p_es == NULL */)
    {
        QuickTimeGenericRTPSource *qtRTPSource =
            (QuickTimeGenericRTPSource*)sub->rtpSource();
        QuickTimeGenericRTPSource::QTState &qtState = qtRTPSource->qtState;
        uint8_t *sdAtom = (uint8_t*)&qtState.sdAtom[4];

        /* Get codec information from the quicktime atoms :
         * http://developer.apple.com/quicktime/icefloe/dispatch026.html */
        if( track->getFormat().type == "video" ) {
            if( qtState.sdAtomSize < 16 + 32 )
            {
                /* invalid */
                event_data = (char)0xff;
                track->doWaiting(0);
                return;
            }
            track->getFormat().codec = string((char*)sdAtom, 4);;
            track->getFormat().video.i_width  = (sdAtom[28] << 8) | sdAtom[29];
            track->getFormat().video.i_height = (sdAtom[30] << 8) | sdAtom[31];

            if( track->getFormat().codec == "avc1" )
            {
                uint8_t *pos = (uint8_t*)qtRTPSource->qtState.sdAtom + 86;
                uint8_t *endpos = (uint8_t*)qtRTPSource->qtState.sdAtom
                                  + qtRTPSource->qtState.sdAtomSize;
                while (pos+8 < endpos) {
                    unsigned int atomLength = pos[0]<<24 | pos[1]<<16 | pos[2]<<8 | pos[3];
                    if( atomLength == 0 || atomLength > (unsigned int)(endpos-pos)) break;
                    if( memcmp(pos+4, "avcC", 4) == 0 &&
                        atomLength > 8 &&
                        atomLength <= INT_MAX )
                    {
						track->getFormat().extra = string((char*)pos + 8, atomLength - 8);
                        break;
                    }
                    pos += atomLength;
                }
            }
            else{
				track->getFormat().extra.erase(track->getFormat().extra.size()-16, 16);
            }
        }
        else {
            if( qtState.sdAtomSize < 24 )
            {
                /* invalid */
                event_data = (char)0xff;
                track->doWaiting(0);
                return;
            }
			track->getFormat().codec = string((char*)sdAtom, 4);;
			track->getFormat().audio.i_bitspersample = (sdAtom[22] << 8) | sdAtom[23];
        }
        //tk->p_es = es_out_Add( p_demux->out, &tk->fmt );
    }

    /* grow buffer if it looks like buffer is too small, but don't eat
     * up all the memory on strange streams */
    if( i_truncated_bytes > 0 )
    {
        if( track->buffer_size() < 2000000 )
        {
            void *p_tmp;
            //msg_Dbg( p_demux, "lost %d bytes", i_truncated_bytes );
            //msg_Dbg( p_demux, "increasing buffer size to %d", tk->i_buffer * 2 );
            p_tmp = realloc(track->p_buffer, track->i_buffer * 2 );
			track->p_buffer = (uint8_t*)p_tmp;
			track->i_buffer *= 2;
        }
    }

    assert( i_size <= track->buffer_size() );
    unsigned int out_size = i_size;

    if( track->getFormat().codec == "AMR" ||
        track->getFormat().codec == "AMR-WB" )
    {
        AMRAudioSource *amrSource = (AMRAudioSource*)sub->readSource();

        track->buffer()[0] = amrSource->lastFrameHeader();
        out_size++;
    }
    else if( track->getFormat().codec == "H261")
    {
        H261VideoRTPSource *h261Source = (H261VideoRTPSource*)sub->rtpSource();
        uint32_t header = h261Source->lastSpecialHeader();
        memcpy( track->buffer(), &header, 4 );
        out_size += 4;

        //if( sub->rtpSource()->curPacketMarkerBit() )
        //    p_block->i_flags |= BLOCK_FLAG_END_OF_FRAME;
    }
    else if( track->getFormat().codec == "H264" || track->getFormat().codec == "H265" )
    {
        /* Normal NAL type */
        track->buffer()[0] = 0x00;
        track->buffer()[1] = 0x00;
        track->buffer()[2] = 0x00;
        track->buffer()[3] = 0x01;
        out_size += 4;
    }

    if( i_pcr < i_pts )
    {
        i_pcr = i_pts;
    }

    /* Update our global npt value */
    if( track->getNPT() > 0 &&
        ( track->getNPT() < f_npt_length || f_npt_length <= 0 ) )
        f_npt = track->getNPT();

    i_dts = ( track->getFormat().codec == "MPV" ) ? VLC_TS_INVALID : (VLC_TS_0 + i_pts);

    onData(track, track->buffer(), out_size, i_truncated_bytes, i_pts, i_dts);

    /* warn that's ok */
    event_data = (char)0xff;

    /* we have read data */
    track->doWaiting(0);
    b_no_data = false;
    i_no_data_ti = 0;

    if( i_pts > 0 && !track->b_muxed ){
		track->i_pts = i_pts;
    }

	uint8_t* p_buffer = track->buffer();

	if (track->getFormat().codec == "AMR" ||
		track->getFormat().codec == "AMR-WB")
	{
		p_buffer++;
	}
	else if (track->getFormat().codec == "H261" ||
		track->getFormat().codec == "H264" ||
		track->getFormat().codec == "H265")
	{
		p_buffer += 4;
	}

	track->doWaiting(1);
	sub->readSource()->getNextFrame(p_buffer, track->buffer_size(),
		Live555Client::LiveTrack::streamRead, track, Live555Client::LiveTrack::streamClose, track);
}

void Live555Client::onStreamClose(LiveTrack* track)
{
    event_rtsp = (char)0xff;
    event_data = (char)0xff;

	live555ResultCode = HTTP_ERR_EOF;
}


unsigned char* parseH264ConfigStr(char const* configStr,
	unsigned int& configSize)
{
	char *dup, *psz;
	size_t i_records = 1;

	configSize = 0;

	if (configStr == NULL || *configStr == '\0')
		return NULL;

	psz = dup = strdup(configStr);

	/* Count the number of commas */
	for (psz = dup; *psz != '\0'; ++psz)
	{
		if (*psz == ',')
		{
			++i_records;
			*psz = '\0';
		}
	}

	size_t configMax = 5 * strlen(dup);
	unsigned char *cfg = new unsigned char[configMax];
	psz = dup;
	for (size_t i = 0; i < i_records; ++i)
	{
		cfg[configSize++] = 0x00;
		cfg[configSize++] = 0x00;
		cfg[configSize++] = 0x00;
		cfg[configSize++] = 0x01;

		configSize += vlc_b64_decode_binary_to_buffer(cfg + configSize,
			configMax - configSize, psz);
		psz += strlen(psz) + 1;
	}

	free(dup);
	return cfg;
}

uint8_t * parseVorbisConfigStr(char const* configStr,
	unsigned int& configSize)
{
	configSize = 0;
	if (configStr == NULL || *configStr == '\0')
		return NULL;
#if LIVEMEDIA_LIBRARY_VERSION_INT >= 1332115200 // 2012.03.20
	unsigned char *p_cfg = base64Decode(configStr, configSize);
#else
	char* configStr_dup = strdup(configStr);
	unsigned char *p_cfg = base64Decode(configStr_dup, configSize);
	free(configStr_dup);
#endif
	uint8_t *p_extra = NULL;
	/* skip header count, ident number and length (cf. RFC 5215) */
	const unsigned int headerSkip = 9;
	if (configSize > headerSkip && ((uint8_t*)p_cfg)[3] == 1)
	{
		configSize -= headerSkip;
		p_extra = new uint8_t[configSize];
		memcpy(p_extra, p_cfg + headerSkip, configSize);
	}
	delete[] p_cfg;
	return p_extra;
}

int HttpErrToRtspErr(int http)
{
    if (http == HTTP_TIMEOUT)               return RTSP_TIMEOUT;
    if (http == HTTP_ERR_USR)               return RTSP_ERR;
    if (http == HTTP_ERR_EOF)               return RTSP_EOF;
    if (http == HTTP_STREAM_NOT_FOUND)      return RTSP_TIMEOUT;
    if (http == HTTP_SESSION_NOT_FOUND)     return RTSP_TIMEOUT;
    if (http == HTTP_UNSUPPORTED_TRANSPOR)  return RTSP_TIMEOUT;
    if (http == HTTP_OK)                    return RTSP_OK;

    return RTSP_ERR;
}

/*
listTracks select删掉 
*/