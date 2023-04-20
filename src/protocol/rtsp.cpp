﻿
#include "rtsp.h"
#include <filesystem>
#include "utils/util.h"
#include "utils/parseUrl.h"
#include "AVPacket/AVReadPacket.h"
#include "rtspSendData.h"
#include "rtspReceiveData.h"

#include "log/logger.h"


char Rtsp::response[2048]{0};


int Rtsp::init(SOCKET socket) {
    clientSocket = socket;
    return 0;
}


int Rtsp::responseData(int status, const std::string &msg) {
    memset(response, 0, 1024);
    sprintf(response,
            "RTSP/1.0 %d %s\r\n"
            "CSeq: %s\r\n"
            "Date: %s\r\n"
            "Server: XiaoFeng\r\n"
            "Connection: Close\r\n"
            "\r\n",
            status, msg.c_str(),
            obj["CSeq"].c_str(),
            generatorDate().c_str()
    );

    int ret = TcpSocket::sendData(clientSocket, reinterpret_cast<uint8_t *>(response),
                                  static_cast<int>(strlen(response)));
    if (ret < 0) {
        fprintf(stderr, "发送数据失败\n");
        return ret;
    }
    return 0;
}


int Rtsp::parseRtsp(std::string &msg, const std::string &data) {
    int ret;

    memset(response, 0, 2048);
    std::vector<std::string> list = split(data, "\r\n");
    char method[20]{0};
    char url[50]{0};
    char protocolVersion[20]{0};


    int num = sscanf(list.front().c_str(), "%s %s %s\r\n", method, url, protocolVersion);
    if (num != 3) {
        responseData(500, "不是rtsp协议");
        fprintf(stderr, "解析method, url, version失败\n");
        return -1;
    }
    ParseUrl urlUtils(url, 554);
    urlUtils.parse();

    list.erase(list.begin());

    obj = getObj(list, ":");

    if (strcmp(method, "OPTIONS") == 0) {
        /*回复客户端当前可用的方法*/
        sprintf(response,
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %s\r\n"
                "Date: %s\r\n"
                "Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, ANNOUNCE, RECORD, SET_PARAMETER, GET_PARAMETER\r\n"
                "Server: XiaoFeng\r\n"
                "\r\n",
                obj["CSeq"].c_str(),
                generatorDate().c_str()
        );

        ret = TcpSocket::sendData(clientSocket, reinterpret_cast<uint8_t *>(response),
                                  static_cast<int>(strlen(response)));
        if (ret < 0) {
            fprintf(stderr, "发送数据失败\n");
            return ret;
        }
    } else if (strcmp(method, "DESCRIBE") == 0) {
        char sdp[1024]{0};
        /*v=协议版本，一般都是0*/
        /*
         * o=<username> <session-id> <session-version> <nettype> <addrtype> <unicast-address>
         * username：发起者的用户名，不允许存在空格，如果应用不支持用户名，则为-
         * sess-id：会话id，由应用自行定义，规范的建议是NTP(Network Time Protocol)时间戳。
         * sess-version：会话版本，用途由应用自行定义，只要会话数据发生变化时比如编码，sess-version随着递增就行。同样的，规范的建议是NTP时间戳
         * nettype：网络类型，比如IN表示Internet
         * addrtype：地址类型，比如IP4、IV6
         * unicast-address：域名，或者IP地址
         * */
        /*
         * 声明会话的开始、结束时间
         * t=<start-time> <stop-time>
         * 如果<stop-time>是0，表示会话没有结束的边界，但是需要在<start-time>之后会话才是活跃(active)的。
         * 如果<start-time>是0，表示会话是永久的。
         * */

        /*
         * 如果会话级描述为a=control:*，
         * 那么媒体级control后面跟的就是相对于从Content-Base，Content-Location，request URL中去获得基路径
         * request URL指DESCRIBE时的url
         * */

        /*
         * 如果FMTP中的SizeLength参数设置为13，那么它指定了每个NAL单元头部中长度字段的长度为13个比特。
         这意味着，每个NAL单元头部中包含的长度信息将用13位二进制数表示，可以表示的最大值为8191（2的13次方减1）。
        这个具体的设置可能是针对某种特定的视频编码标准或压缩方案而设定的，
         因为不同的编码标准和方案可能需要不同的位数来表示NAL单元的长度信息。
         因此，通过在FMTP中指定SizeLength参数，可以确保解码器正确地解析每个NAL单元，
         并恢复出正确的视频帧数据，以便实现正确的视频播放*/

        /*
profile-level-id=1：表示音频的编码格式是MPEG-4 AAC。
mode=AAC-hbr：表示音频的传输模式是AAC高比特率（high bitrate）。
sizelength=13：表示音频的访问单元（AU）头部的长度是13位。
indexlength=3：表示音频的访问单元索引（AU-Index）的长度是3位。
indexdeltalength=3：表示音频的访问单元索引差值（AU-Index-delta）的长度是3位。
         */

        /*
         * packetization-mode
            0：单 NALU 模式。视频流中的每个 NALU 都被分配到单独的 RTP 包中。
            1：分片模式。视频流一个NALU会切割分到不同的rtp包中
         * */
        std::string path = "." + urlUtils.getPath();
        if (!std::filesystem::exists(path)) {
            fprintf(stderr, "目录不存在\n");
            responseData(404, "找不到流");
            return -1;
        }

        dir = path;
        for (const std::filesystem::directory_entry &entry: std::filesystem::directory_iterator(path)) {
            std::string extension = entry.path().extension().string();
            if (extension == ".ts") {
                std::string filename = entry.path().filename().string();
                std::size_t start = filename.find("test") + 4;
                std::size_t end = filename.find(".ts");
                int number = std::stoi(filename.substr(start, end - start));
                if (number > transportStreamPacketNumber) {
                    transportStreamPacketNumber = number;
                }
            }
        }


        AVReadPacket packet;
        ret = packet.init(dir, transportStreamPacketNumber);
        if (ret < 0) {
            fprintf(stderr, "packet.init 初始化失败\n");
            return ret;
        }
        ret = packet.getParameter();
        if (ret < 0) {
            fprintf(stderr, "packet.getParameter 失败\n");
            return ret;
        }
        std::string sprop_parameter_sets = GenerateSpropParameterSets(packet.spsData, packet.spsSize,
                                                                      packet.ppsData, packet.ppsSize);

        std::string profileLevelId;
        profileLevelId += decimalToHex(packet.sps.profile_idc);
        profileLevelId += decimalToHex(packet.sps.compatibility);
        profileLevelId += decimalToHex(packet.sps.level_idc);


        uint32_t sample_rate = packet.aacHeader.sample_rate;
        uint8_t channel_configuration = packet.aacHeader.channel_configuration;
        uint8_t profile = packet.aacHeader.profile + 1;

        uint8_t buffer[2];
        WriteStream ws(buffer, 2);
        ws.writeMultiBit(5, profile);
        ws.writeMultiBit(4, packet.aacHeader.sampling_frequency_index);
        ws.writeMultiBit(4, channel_configuration);

        uint8_t frameLengthFlag = 0;
        uint8_t dependsOnCoreCoder = 0;
        uint8_t extensionFlag = 0;
        ws.writeMultiBit(1, frameLengthFlag);
        ws.writeMultiBit(1, dependsOnCoreCoder);
        ws.writeMultiBit(1, extensionFlag);
        std::string audioConfig;
        audioConfig += decimalToHex(buffer[0]);
        audioConfig += decimalToHex(buffer[1]);


        videoControl = "streamId=0";
        audioControl = "streamId=1";
        sprintf(sdp,
                "v=0\r\n"
                "o=- 9%lld 1 IN IP4 %s\r\n"
                "t=0 0\r\n"
                "a=control:*\r\n"
                "m=video 0 RTP/AVP 96\r\n"
                "a=rtpmap:96 H264/90000\r\n"
                "a=fmtp:96 packetization-mode=1;sprop-parameter-sets=%s;profile-level-id=%s\r\n"
                "a=control:%s\r\n"
                "m=audio 0 RTP/AVP 97\r\n"
                "a=rtpmap:97 mpeg4-generic/%s/%s\r\n"
                "a=fmtp:97 SizeLength=13;IndexLength=3;IndexDeltaLength=3;config=%s\r\n"
                "a=control:%s\r\n",
                time(nullptr), urlUtils.getDomain().c_str(),
                sprop_parameter_sets.c_str(), profileLevelId.c_str(),
                videoControl.c_str(),
                std::to_string(sample_rate).c_str(), std::to_string(channel_configuration).c_str(),
                audioConfig.c_str(),
                audioControl.c_str()
        );

        /*给每个拉流的生成一个唯一id*/
        uniqueSession = generate_unique_string();
        /*上面这里使用从推流端传过来的信息，在sdpInfo里，然后返回给拉流端*/
        /*%zu 为无符号整型*/
        sprintf(response,
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %s\r\n"
                "Content-Base: %s\r\n"
                "Content-type: application/sdp\r\n"
                "Content-length: %zu\r\n"
                "Session: %s\r\n"
                "\r\n"
                "%s",
                obj["CSeq"].c_str(),
                url,
                strlen(sdp),
                uniqueSession.c_str(),
                sdp
        );
        ret = TcpSocket::sendData(clientSocket, reinterpret_cast<uint8_t *>(response),
                                  static_cast<int>(strlen(response)));
        if (ret < 0) {
            fprintf(stderr, "发送数据失败\n");
            return ret;
        }
    } else if (strcmp(method, "ANNOUNCE") == 0) {
        /*检测是否是推流上来的，断开的时候要删除目录*/
        flag = true;
        dir = "." + urlUtils.getPath();
        if (std::filesystem::exists(dir)) {
            fprintf(stderr, "目录已存在\n");
            responseData(500, "换个推流地址");
            return -1;
        }


        if (obj.count("Content-Length") > 0) {
            int size = std::stoi(obj["Content-Length"]);

            while (true) {
                if (msg.length() >= size) {
                    std::string sdp = msg.substr(0, size);
                    msg.erase(0, size);

                    /*推流上来会生成一个唯一的session*/
                    uniqueSession = generate_unique_string();

                    ret = parseSdp(sdp, info);
                    if (ret < 0) {
                        responseData(500, "解析SDP失败");
                        return -1;
                    }
                    for (auto &media: info.media) {
                        if (media["type"] == "video") {
                            videoControl = media["control"];
                        } else if (media["type"] == "audio") {
                            audioControl = media["control"];
                        }
                    }

                    sprintf(response,
                            "RTSP/1.0 200 OK\r\n"
                            "CSeq: %s\r\n"
                            "Date: %s\r\n"
                            "Server: XiaoFeng\r\n"
                            "Session: %s\r\n"
                            "\r\n",
                            obj["CSeq"].c_str(),
                            generatorDate().c_str(),
                            uniqueSession.c_str()
                    );
                    ret = TcpSocket::sendData(clientSocket, reinterpret_cast<uint8_t *>(response),
                                              static_cast<int>(strlen(response)));
                    if (ret < 0) {
                        fprintf(stderr, "发送数据失败\n");
                        return ret;
                    }
                    break;
                } else {

                    char buffer[TcpSocket::MAX_BUFFER];
                    int length = 0;
                    ret = TcpSocket::receive(clientSocket, buffer, length);
                    if (ret < 0) {
                        fprintf(stderr, "rtsp.receive failed\n");;
                        return ret;
                    }
                    /*把这次读取到的追加到packet*/
                    msg.append(buffer, length);
                }
            }

        } else {
            fprintf(stderr, "ANNOUNCE 请求里没有SDP信息\n");
            responseData(451, "ANNOUNCE 请求里没有SDP信息");
            return -1;
        }


    } else if (strcmp(method, "SETUP") == 0) { //客户端发送建立请求，请求建立连接会话，准备接收音视频数据

        char protocol[30]{0};
        char type[30]{0};
        char interleaved[30]{0};
        num = sscanf(obj["Transport"].c_str(), "%[^;];%[^;];%[^;]", protocol, type, interleaved);
        if (num != 3) {
            fprintf(stderr, "解析Transport错误 = %s\n", obj["Transport"].c_str());
            responseData(500, "解析Transport错误");
            return -1;
        }
        if (strcmp(protocol, "RTP/AVP/TCP") != 0) {
            fprintf(stderr, "只支持TCP传输\n");
            responseData(501, "只支持TCP传输");
            return -1;
        }


        uint8_t rtpChannel = 0;
        uint8_t rtcpChannel = 0;
        num = sscanf(interleaved, "interleaved=%hhu-%hhu", &rtpChannel, &rtcpChannel);
        if (num != 2) {
            fprintf(stderr, "解析port错误  %s\n", interleaved);
            responseData(451, "解析port错误");
            return -1;
        }

        // 截取第三个斜杠后面的子串，就是路径部分
        std::string path = urlUtils.getPath();
        std::vector<std::string> pathList = split(path, "/");

        /*如果是推流，rtpChannel 表示推流上来的数据是音频还是视频，推流就用ANNOUNCE请求里的control字段来判断当前SETUP请求里rtpChannel是音频还是视频*/
        /*如果是拉流，rtpChannel 表示拉流的数据是音频还是视频，拉流就用我发送给客户端的DESCRIBE请求里的control字段来判断当前SETUP请求里rtpChannel是音频还是视频*/
        if (pathList.back() == videoControl) {
            videoChannel = rtpChannel;
        } else if (pathList.back() == audioControl) {
            audioChannel = rtpChannel;
        } else {
            fprintf(stderr, "没找到对应的流 -- %s\n", pathList.back().c_str());
            responseData(500, "没找到对应的流");
            return -1;
        }


        sprintf(response,
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %s\r\n"
                "Date: %s\r\n"
                "Server: XiaoFeng\r\n"
                "Transport: %s;%s;%s\r\n"
                "Session: %s\r\n"
                "\r\n",
                obj["CSeq"].c_str(),
                generatorDate().c_str(),
                protocol, type, interleaved,
                obj["Session"].c_str()
        );
        ret = TcpSocket::sendData(clientSocket, reinterpret_cast<uint8_t *>(response),
                                  static_cast<int>(strlen(response)));
        if (ret < 0) {
            fprintf(stderr, "发送数据失败\n");
            return ret;
        }

    } else if (strcmp(method, "RECORD") == 0) {
        sprintf(response,
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %s\r\n"
                "Date: %s\r\n"
                "Server: XiaoFeng\r\n"
                "Session: %s\r\n"
                "\r\n",
                obj["CSeq"].c_str(),
                generatorDate().c_str(),
                obj["Session"].c_str()
        );

        ret = TcpSocket::sendData(clientSocket, reinterpret_cast<uint8_t *>(response),
                                  static_cast<int>(strlen(response)));
        if (ret < 0) {
            fprintf(stderr, "发送数据失败\n");
            return ret;
        }

        /*这里接收客户端传过来的音视频数据，组帧*/
        ret = receiveData(msg);
        if (ret < 0) {
            responseData(500, "接收音视频数据失败");
            return ret;
        }

    } else if (strcmp(method, "PLAY") == 0) {

        sprintf(response,
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %s\r\n"
                "Range: npt=0.000-\r\n"
                "Session: %s; timeout=60\r\n"
                "\r\n",
                obj["CSeq"].c_str(),
                obj["Session"].c_str()
        );
        ret = TcpSocket::sendData(clientSocket, reinterpret_cast<uint8_t *>(response),
                                  static_cast<int>(strlen(response)));
        if (ret < 0) {
            fprintf(stderr, "发送数据失败\n");
            return ret;
        }
        SendThread = new std::thread(&Rtsp::sendData, this);

    } else if (strcmp(method, "TEARDOWN") == 0) {
        //结束会话请求，该请求会停止所有媒体流，并释放服务器上的相关会话数据。
        /*这里推流端和拉流端都会发送这个请求，要各自释放各自的资源*/


        printf("发送了结束会话请求，！！！！！！！！！！！\n");
        sprintf(response,
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %s\r\n"
                "Date: %s\r\n"
                "Server: XiaoFeng\r\n"
                "Session: %s\r\n"
                "\r\n",
                obj["CSeq"].c_str(),
                generatorDate().c_str(),
                obj["Session"].c_str()
        );
        ret = TcpSocket::sendData(clientSocket, reinterpret_cast<uint8_t *>(response),
                                  static_cast<int>(strlen(response)));
        if (ret < 0) {
            fprintf(stderr, "发送数据失败\n");
            return ret;
        }
        /*用户发送请求，结束会话，并且释放服务器资源*/
        stopSendFlag = false;
        stopFlag = false;
    } else if (strcmp(method, "GET_PARAMETER") == 0) {
        printf("method = %s\n", method);
        printf("url = %s\n", url);
        /*保持连接用的*/
        /*在暂停流媒体播放，定期发送GET_PARAMETER作为心跳包维持连接*/
        sprintf(response,
                "RTSP/1.0 200 OK\r\n"
                "CSeq: %s\r\n"
                "Date: %s\r\n"
                "Server: XiaoFeng\r\n"
                "Session: %s\r\n"
                "\r\n",
                obj["CSeq"].c_str(),
                generatorDate().c_str(),
                obj["Session"].c_str()
        );
        ret = TcpSocket::sendData(clientSocket, reinterpret_cast<uint8_t *>(response),
                                  static_cast<int>(strlen(response)));
        if (ret < 0) {
            fprintf(stderr, "发送数据失败 -> option\n");
            return ret;
        }
    } else {
        fprintf(stderr, "不支持解析method = %s\n", method);

        responseData(500, "不支持解析method=" + std::string(method));
        return -1;
    }
    return 0;
}

int Rtsp::sendData() {
    int ret;
    RtpPacket rtpPacket;
    rtpPacket.init();


    AVReadPacket packet;
    ret = packet.init(dir, transportStreamPacketNumber);
    if (ret < 0) {
        fprintf(stderr, "packet.init 初始化失败\n");
        return ret;
    }

    AVPackage *package = AVReadPacket::allocPacket();
    while (stopSendFlag) {
        ret = packet.readFrame(package);
        if (ret < 0) {
            fprintf(stderr, "packet.readFrame 失败\n");
            return -1;
        }
        if (package->type == "video") {
            rtpPacket.timestamp = av_rescale_q(package->decodeFrameNumber, {1, static_cast<int>(package->fps)},
                                               {1, 90000});
            for (int i = 0; i < package->data1.size(); ++i) {
                const Frame &nalUint = package->data1[i];
                ret = rtpPacket.sendVideoFrame(clientSocket, nalUint.data, nalUint.nalUintSize,
                                               i == (package->data1.size() - 1), videoChannel);
                if (ret < 0) {
                    fprintf(stderr, "发送视频数据失败\n");
                    stopFlag = false;
                    return ret;
                }
            }
        } else if (package->type == "audio") {
            rtpPacket.timestamp = package->dts;
            /*这里是把音频数据发出去*/
            ret = rtpPacket.sendAudioPacket(clientSocket, package->data2, package->size, audioChannel);
            if (ret < 0) {
                fprintf(stderr, "发送音频包失败\n");
                stopFlag = false;
                return ret;
            }
        }

    }


    return 0;
}

int Rtsp::receiveData(std::string &msg) {
    int ret;

    RtspReceiveData receive;
    ret = receive.init(clientSocket, dir, videoChannel, audioChannel);
    if (ret < 0) {
        log_error("receive.init失败");
        return ret;
    }

    /*写入sps和pps*/
    ret = receive.writeVideoData(info.spsData, info.spsSize);
    if (ret < 0) {
        log_error("写入sps失败");
        return ret;
    }
    ret = receive.writeVideoData(info.ppsData, info.ppsSize);
    if (ret < 0) {
        log_error("写入pps失败");
        return ret;
    }

    /*写入adts header*/
    ret = receive.writeAudioData(info.audioObjectType, info.samplingFrequencyIndex,
                                 info.channelConfiguration);
    if (ret < 0) {
        log_error("写入音频信息失败");
        return ret;
    }
    ret = receive.receiveData(msg);
    if (ret < 0) {
        log_error("接收推流数据失败");
        return ret;
    }


    return 0;
}


static constexpr uint8_t startCode[4] = {0, 0, 0, 1};

int Rtsp::parseSdp(const std::string &sdp, SdpInfo &sdpInfo) {
    int ret;
    std::vector<std::string> list = split(sdp, "\r\n");

    for (int i = 0; i < list.size(); ++i) {
        const std::string &line = list[i];

        std::string::size_type pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1, line.length() - 2);
            if (key == "v") {
                sdpInfo.version = value;
            } else if (key == "o") {
                std::vector<std::string> originList = split(value, " ");
                sdpInfo.origin["username"] = originList[0];
                sdpInfo.origin["sessionId"] = originList[1];
                sdpInfo.origin["sessionVersion"] = originList[2];
                sdpInfo.origin["netType"] = originList[3];
                sdpInfo.origin["ipVer"] = originList[4];
                sdpInfo.origin["address"] = originList[5];
            } else if (key == "s") {
                /*是一个必需的字段,表示会话名称，可以是任意字符串*/
                sdpInfo.name = value;
            } else if (key == "t") {
                /*是一个必需的字段，它表示会话的时间信息。它包括两个或多个时间戳，分别表示会话的开始时间和结束时间。*/
                std::vector<std::string> timingList = split(value, " ");
                sdpInfo.timing["start"] = timingList[0];
                sdpInfo.timing["stop"] = timingList[1];
            } else if (key == "m") {
                /*遇到媒体层了*/
                ret = parseMediaLevel(i, list, sdpInfo);
                if (ret < 0) {
                    return ret;
                }
                break;
            }

        }
    }
    return 0;
}


int Rtsp::parseMediaLevel(int i, const std::vector<std::string> &list, SdpInfo &sdpInfo) {
    int ret;
    int num = -1;
    for (int j = i; j < list.size(); ++j) {
        const std::string &line = list[j];

        std::string::size_type pos = line.find('=');

        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            if (key == "m") {
                ++num;
                std::vector<std::string> mediaList = split(value, " ");
                sdpInfo.media[num]["type"] = mediaList[0];
                sdpInfo.media[num]["port"] = mediaList[1];
                sdpInfo.media[num]["protocol"] = mediaList[2];
                sdpInfo.media[num]["payloads"] = mediaList[3];

            } else if (key == "a") {
                std::string::size_type position = value.find(' ');
                if (position != std::string::npos) {
                    std::string left = value.substr(0, position);
                    std::string right = value.substr(position + 1);
                    if (sdpInfo.media[num]["type"] == "video" && sdpInfo.media[num]["payloads"] == "96") {
                        if (left == "rtpmap:96") {
                            sdpInfo.media[num]["rtpmap"] = right;
                        } else if (left == "fmtp:96") {
                            sdpInfo.media[num]["fmtp"] = right;
                            // fmtp:96 packetization-mode=1; sprop-parameter-sets=Z2QAH6zZQFAFuhAAAAMAEAAAAwPA8YMZYA==,aOvjyyLA; profile-level-id=64001F
                            std::map<std::string, std::string> obj = getObj(split(right, ";"), "=");
                            std::vector<std::string> sps_pps = split(obj["sprop-parameter-sets"], ",");


                            memcpy(sdpInfo.spsData, startCode, 4);
                            ret = base64_decode(sps_pps[0], sdpInfo.spsData + 4);
                            if (ret < 0) {
                                fprintf(stderr, "解析sps失败\n");
                                return ret;
                            }
                            sdpInfo.spsSize = ret + 4;

                            memcpy(sdpInfo.ppsData, startCode, 4);
                            ret = base64_decode(sps_pps[1], sdpInfo.ppsData + 4);
                            if (ret < 0) {
                                fprintf(stderr, "解析pps失败\n");
                                return ret;
                            }
                            sdpInfo.ppsSize = ret + 4;
                        } else {
                            fprintf(stderr, "解析错误\n");
                            return -1;
                        }
                    } else if (sdpInfo.media[num]["type"] == "audio" && sdpInfo.media[num]["payloads"] == "97") {
                        if (left == "rtpmap:97") {
                            sdpInfo.media[num]["rtpmap"] = right;
                        } else if (left == "fmtp:97") {
                            //                            fmtp:97 profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3; config=119056E500
                            std::map<std::string, std::string> obj = getObj(split(right, ";"), "=");
                            sdpInfo.media[num]["fmtp"] = right;
                            ret = parseAACConfig(obj["config"], sdpInfo);
                            if (ret < 0) {
                                return ret;
                            }
                        } else {
                            fprintf(stderr, "解析错误\n");
                            return -1;
                        }
                    }

                } else {
                    /*这里默认认为他是a=control,有可能有bug*/
                    std::vector<std::string> controlList = split(value, ":");
                    sdpInfo.media[num]["control"] = controlList[1];
                }
            }
        }
    }
    return 0;
}


int Rtsp::parseAACConfig(const std::string &config, SdpInfo &sdpInfo) {
    // 把十六进制字符串转换成二进制字符串
    std::string bin;
    for (char i: config) {
        switch (i) {
            case '0':
                bin.append("0000");
                break;
            case '1':
                bin.append("0001");
                break;
            case '2':
                bin.append("0010");
                break;
            case '3':
                bin.append("0011");
                break;
            case '4':
                bin.append("0100");
                break;
            case '5':
                bin.append("0101");
                break;
            case '6':
                bin.append("0110");
                break;
            case '7':
                bin.append("0111");
                break;
            case '8':
                bin.append("1000");
                break;
            case '9':
                bin.append("1001");
                break;
            case 'A':
                bin.append("1010");
                break;
            case 'B':
                bin.append("1011");
                break;
            case 'C':
                bin.append("1100");
                break;
            case 'D':
                bin.append("1101");
                break;
            case 'E':
                bin.append("1110");
                break;
            case 'F':
                bin.append("1111");
                break;
            default:
                fprintf(stderr, "输入的十六进制有问题\n");
                return -1;
        }
    }

    // 按照MPEG-4音频标准中的规则，把二进制字符串分成以下几个字段，并输出它们的值
    sdpInfo.audioObjectType = std::stoi(bin.substr(0, 5), nullptr, 2); // 前5位，表示音频对象类型（Audio Object Type）
    sdpInfo.samplingFrequencyIndex = std::stoi(bin.substr(5, 4), nullptr, 2); // 后4位，表示采样率索引（Sampling Frequency Index）
    sdpInfo.channelConfiguration = std::stoi(bin.substr(9, 4), nullptr, 2); // 后4位，表示声道配置（Channel Configuration）
    uint8_t GASpecificConfig = std::stoi(bin.substr(13, 3), nullptr, 2); // 后3位，表示全局音频特定配置信息




    return 0;
}


Rtsp::~Rtsp() {

    if (flag) {
        std::filesystem::remove_all(dir);
    }

    stopSendFlag = false;
    if (SendThread && SendThread->joinable()) {
        SendThread->join();
        delete SendThread;
    }
    log_info("Rtsp 析构");
}












