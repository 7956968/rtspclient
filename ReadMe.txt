特性:
        完整的RTSP特性(SEEK 设置播放速度等)
        自动重连
        实现第三方流发送(sdp中带上destination,需要服务端支持,live555需要打开编译开关RTSP_ALLOW_CLIENT_DESTINATION_SETTING)
        消除RTCP校正时间戳带来的突变
	

贡献者列表:
        青瓜王子(240686938)
        过客(29057025)

依赖:
	live555

例子依赖:
        c++11(vs2015)

项目状态:
	不稳定