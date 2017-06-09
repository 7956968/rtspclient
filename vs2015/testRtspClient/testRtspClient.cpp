// testRtspClient.cpp : �������̨Ӧ�ó������ڵ㡣
//

#include "stdafx.h"

#include <sstream>
#include <iostream>
#include <stdio.h>
#include <functional>
#include <thread>


#include "../../Live555Client.h"

using namespace std;

class DemuxLive555 : public Live555Client
{
public:
	virtual void onInitializedTrack(LiveTrack* track)
	{
		LiveTrack::media_format& fmt = track->getFormat();
		std::string SessionName = track->getSessionName();

		if (fmt.type == "video") {
			cout << "video:" << SessionName << endl;

		}
		if (fmt.type == "audio") {
			cout << "aduio:" << SessionName << endl;
		}
	}

	void Loop()
	{
		int statu = RTSP_OK;

		while (m_MyLoop)
		{
			statu = PlayRtsp(m_Uri);

			if (statu == RTSP_TIMEOUT) {
				std::cout << "timeout" << std::endl;

				if (m_ReconnectOnTimeOut) {
					std::cout << "reconnect" << std::endl;
				}
				else {
					std::cout << "quit" << std::endl;
					break;
				}
			}
			else if(statu == RTSP_EOF) {
				std::cout << "rtsp eof" << std::endl;
				break;
				//do what you want
			}
			else if (statu == RTSP_ERR) {
				std::cout << "rtsp err" << std::endl;
				break;
			}
			else if (statu == RTSP_USR_STOP) {
				std::cout << "usr stop" << std::endl;
				break;
			}
			else if (statu == RTSP_OK) {

			}
			else {
				std::cout << "weird code:" << statu << std::endl;

			}
			//��������ٴ�����
			std::this_thread::sleep_for(std::chrono::seconds(m_ReconnectGap));
		}

		std::cout << "rtsp thread quit" << std::endl;
		m_MyLoop = false;
	}

	void Play(std::string Uri)
	{
		m_Uri = Uri;
		m_MyLoop = true;

		std::thread demuxThread(&DemuxLive555::Loop, this);
		m_demuxLoop = std::move(demuxThread);
	}

	void Stop()
	{
		if (m_MyLoop == false) return;	//�Ѿ��˳�

		//�������Լ��ı�־λ
		m_MyLoop = 0;
		//ֹͣ��ѭ��
		StopRtsp();
		//�ȴ��߳̽���
		m_demuxLoop.join();
	}

	virtual void onData(LiveTrack* track,
		uint8_t* p_buffer,
		int i_size,
		int i_truncated_bytes,
		int64_t pts, int64_t dts)
	{
		if (PrintTs) {
			std::string SessionName = track->getSessionName();
			int64_t deltaPts = pts - lastPts;
			int64_t deltaDts = dts - lastDts;
			cout << SessionName << "," << i_size << ","
				<< "pts:" << pts << ",dt:" << deltaPts << ","
				<< "dts:" << dts << ",dt:" << deltaDts << endl;

			lastPts = pts;
			lastDts = dts;
		}
		else {
			cout << '.';
		}

		LiveTrack::media_format& fmt = track->getFormat();

		//�����H264�ͱ���
		if ((DstFile.size() > 0) && (fmt.type == "video")
			&& (fmt.codec == "H264")) {
			FILE* fp = fopen(DstFile.c_str(), "ab");
			fwrite(p_buffer, i_size, 1, fp);
			fclose(fp);
		}
	}
	string DstFile;

	bool PrintTs = true;

	int64_t lastPts = 0, lastDts = 0;

	std::string m_UsrName;
	std::string m_Password;
	std::string m_Uri;
	std::thread m_demuxLoop;
	int m_ReconnectOnTimeOut = true;
	int m_ReconnectGap = 3;				//�������,��
	volatile bool m_MyLoop = false;
};


using namespace std::chrono;
using namespace std;

struct RtspTest
{
	//���Կ���
	int bTest;
	//����,���ڴ�ӡ
	const char* describe;
	//���Ժ���
	std::function<void(void)> f;
};

int main()
{
	const char* normalRtspAddr  = "rtsp://192.168.103.43/ch0.liv";
	const char* normalRtspAddr2 = "rtsp://192.168.103.46/ch0.liv";
	const char* NoExistRtspAddr = "rtsp://192.168.103.99/ch0.liv";
	//1�������ڵĶ��ļ���Live555��Server
	const char* RtspAddrWileEof = "rtsp://127.0.0.1/h264RtspTestSave.264";

	RtspTest NowTest[] =
	{
		{0,"����һ�������ڵĵ�ַ��Ѹ�ٹر�",[&] {
			DemuxLive555 demux0;

			demux0.Play(NoExistRtspAddr);

			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}

			demux0.Stop();
		} },

		{0,"����һ�������ڵĵ�ַ�����Բ�������10���ر�",[&] {
			DemuxLive555 demux0;

			demux0.Play(NoExistRtspAddr);

			{
				std::this_thread::sleep_for(std::chrono::seconds(10));
			}

			demux0.Stop();
		}},

		{0,"����һ�����ڵĵ�ַ��Ѹ�ٹر�",[&] {
			DemuxLive555 demux0;

			demux0.Play(normalRtspAddr);

			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}

			demux0.Stop();
		}},

		{0,"����һ�����ڵĵ�ַ������10���ر�",[&] {
			DemuxLive555 demux0;

			demux0.Play(normalRtspAddr);

			{
				std::this_thread::sleep_for(std::chrono::seconds(10));
			}

			demux0.Stop();
		}},

		{0,"��·���Ĳ���ֹͣ",[&] {
			int testTime = 5;
			while (testTime-- > 0)
			{
				DemuxLive555 demux1;

				demux1.Play(normalRtspAddr);

				{
					std::this_thread::sleep_for(std::chrono::seconds(5));
				}

				demux1.Stop();
			}
		}},

		{0,"���Զ�·",[&] {
			int testTime = 5;
			while (testTime-- > 0)
			{
				DemuxLive555 demux1;
				DemuxLive555 demux2;

				demux1.Play(normalRtspAddr);
				demux2.Play(normalRtspAddr2);

				{
					std::this_thread::sleep_for(std::chrono::seconds(5));
				}

				demux1.Stop();
				demux2.Stop();
			}
		}},

		{0,"���Ե�·H264����������",[&] {
			DemuxLive555 demux1;
			demux1.DstFile = "h264RtspTestSave.h264";
			demux1.Play(normalRtspAddr);

			{
				std::this_thread::sleep_for(std::chrono::seconds(10));
			}

			demux1.Stop();
		}},

		{ 0,"���Զ�·H264����������",[&] {
			DemuxLive555 demux1;
			demux1.PrintTs = false;
			demux1.DstFile = "h264RtspTestSave1.h264";
			demux1.Play(normalRtspAddr);

			DemuxLive555 demux2;
			demux2.PrintTs = false;
			demux2.DstFile = "h264RtspTestSave2.h264";
			demux2.Play(normalRtspAddr2);

			{
				std::this_thread::sleep_for(std::chrono::seconds(20));
			}
			demux1.Stop();
			demux2.Stop();
		} },

		{ 1,"����RTSP����EOF",[&] {
			DemuxLive555 demux0;
			demux0.PrintTs = false;
			demux0.Play(RtspAddrWileEof);

			{
				std::this_thread::sleep_for(std::chrono::seconds(100));
			}

			demux0.Stop();
		} },
	};

	int size = sizeof(NowTest) / sizeof(RtspTest);
	for (int i = 0; i < size; i++) {
		if (NowTest[i].bTest) {
			printf("%s\n", NowTest[i].describe);
			NowTest[i].f();
		}
	}

	printf(" �������\n");

	cin.get();

	return 0;
}

