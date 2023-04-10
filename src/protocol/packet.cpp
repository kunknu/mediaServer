﻿

#include "packet.h"
#include <thread>
#include <filesystem>
#include "readStream.h"
#include "NALPicture.h"

int AVPacket::init(const std::string &dir, uint32_t transportStreamPacketNumber) {
    path = dir;
    currentPacket = transportStreamPacketNumber;


    std::string name = "/test" + std::to_string(currentPacket) + ".ts";
    printf("读取%s文件 video init\n", name.c_str());
    fs.open(path + name, std::ios::out | std::ios::binary);
    if (!fs.is_open()) {
        fprintf(stderr, "open %s failed\n", name.c_str());
        return -1;
    }
    videoReader.init2();
    /*todo*/
    audioReader.init2();


    picture = videoReader.allocPicture();
    return 0;
}

int AVPacket::getTransportStreamData() {
    int ret = 0;
    std::string name;
    uint32_t size = 0;
    while (true) {
        fs.read(reinterpret_cast<char *>(transportStreamBuffer + size), TRANSPORT_STREAM_PACKETS_SIZE - size);
        size += fs.gcount();
        if (size == 0) {
            name = "/test" + std::to_string(++currentPacket) + ".ts";
            if (!std::filesystem::exists(path + name)) {
                /*没有这个文件，说明这个文件还没读完，要继续读这个文件*/
                --currentPacket;
                fs.clear();
                fprintf(stderr, "没有下个文件,等待一会儿\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            /*表示这个文件读完了，读下一个*/
            fs.close();
            printf("读取%s文件 读取的size = %d video\n", name.c_str(), size);
            fs.open(path + name, std::ios::out | std::ios::binary);
            if (!fs.is_open()) {
                fprintf(stderr, "读取%s失败 video\n", name.c_str());
                return -1;
            }

            fs.read(reinterpret_cast<char *>(transportStreamBuffer), TRANSPORT_STREAM_PACKETS_SIZE);
            size = fs.gcount();
            if (size != TRANSPORT_STREAM_PACKETS_SIZE) {
                fprintf(stderr, "没读到一个ts包的大小，read size = %d\n", size);
                return -1;
            }
        } else if (size < TRANSPORT_STREAM_PACKETS_SIZE) {
            fs.clear();
            fprintf(stderr, "size < TRANSPORT_STREAM_PACKETS_SIZE %d, video\n", size);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        /*走到这里肯定有188个字节*/
        ReadStream rs(transportStreamBuffer, size);
        ret = demux.readFrame(rs);
        if (ret < 0) {
            fprintf(stderr, "demux.readVideoFrame失败\n");
            return ret;
        }
        if (ret == VIDEO_PID) {
            size = rs.size - rs.position;
            videoReader.putData(rs.currentPtr, size);
            break;
        } else if (ret == AUDIO_PID) {
            size = rs.size - rs.position;
            audioReader.putData(rs.currentPtr, size);
            break;
        }
        size = 0;
    }


    return ret;
}

int AVPacket::readFrame(Packet *packet) {
    /*这里循环，里面判断是否数据足够一帧了，如果没有足够一帧，那么就继续往里面添加数据，如果有了，就退出循环*/

    int ret;
    header.finishFlag = false;
    picture->finishFlag = false;


    videoReader.resetBuffer();
    audioReader.resetBuffer();

    while (true) {
        ret = getTransportStreamData();
        if (ret < 0) {
            fprintf(stderr, "getTransportStreamData 失败\n");
            return ret;
        }

        if (ret == VIDEO_PID) {
            ret = videoReader.getVideoFrame3(picture);
            if (ret < 0) {
                fprintf(stderr, "videoReader.getVideoFrame3 失败\n");
                return -1;
            }

            if (picture->finishFlag) {
                /*给packet赋值*/
                packet->dts = picture->dts;
                packet->pts = picture->pts;
                packet->data1 = picture->data;
                packet->size = picture->size;
                packet->type = "video";
                break;
            }

        } else if (ret == AUDIO_PID) {
            ret = audioReader.getAudioFrame2(header);
            if (ret < 0) {
                fprintf(stderr, "audioReader.getAudioFrame2 失败\n");
                return -1;
            }

            if (header.finishFlag) {
                packet->dts = header.dts;
                packet->pts = header.pts;
                packet->data2 = header.data;
                packet->size = header.size;
                packet->type = "audio";
                break;
            }
        }

    }
    return 0;
}

Packet *AVPacket::allocPacket() {
    return new Packet;
}

void AVPacket::freePacket(Packet *packet) {
    delete packet;
}