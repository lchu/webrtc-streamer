#include "CdnUploader.h"
#include "rtc_base/logging.h"
// Update FFmpeg includes to use proper directory structure
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

// FFmpeg资源
AVFormatContext* m_outputContext = nullptr;
AVCodecContext* m_codecContext = nullptr;

CdnUploader::CdnUploader(const Json::Value & config) {
    LoadConfig(config);
    avformat_network_init();
    InitFFmpegResources();
}

CdnUploader::~CdnUploader() {
    m_running = false;
    CleanupFFmpegResources();
    avformat_network_deinit();
}

void CdnUploader::InitFFmpegResources() {
    std::lock_guard<std::mutex> lock(m_ffmpegMutex);
    
    // 初始化H.264编码器
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    m_codecContext = avcodec_alloc_context3(codec);
    
    // 配置编码参数
    m_codecContext->bit_rate = m_bitrate;
    m_codecContext->width = m_width;  // 根据实际分辨率动态调整
    m_codecContext->height = m_height;
    m_codecContext->time_base = {1, m_fps};
    m_codecContext->framerate = {m_fps, 1};
    m_codecContext->gop_size = 10;
    m_codecContext->max_b_frames = 1;
    m_codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    
    if (avcodec_open2(m_codecContext, codec, nullptr) < 0) {
        RTC_LOG(LS_ERROR) << "Failed to open codec";
        CleanupFFmpegResources();
    }
}

bool CdnUploader::LoadConfig(const Json::Value & config) {
    if (config.isMember("cdnConfig")) {
        Json::Value filter = m_config["cdnConfig"];
        m_cdnUrl = filter.get("url", "").asString();
        m_width = filter.get("width", "").asString();
        m_height = filter.get("height", "").asString();
    }
    
    RTC_LOG(LS_ERROR) << "Missing url in CDN config";
    return false;
}

void CdnUploader::OnFrame(const webrtc::VideoFrame& frame) {
    if (!m_connected) {
        if (!ConnectToCDN()) return;
    }

    std::lock_guard<std::mutex> lock(m_ffmpegMutex);
    
    // 转换帧格式为YUV420P
    webrtc::VideoFrame converted_frame = frame;
    converted_frame.set_video_frame_buffer(
        converted_frame.video_frame_buffer()->ToI420()
    );

    // 创建AVFrame
    AVFrame* av_frame = av_frame_alloc();
    av_frame->format = AV_PIX_FMT_YUV420P;
    av_frame->width = converted_frame.width();
    av_frame->height = converted_frame.height();
    av_frame->pts = av_rescale_q(converted_frame.timestamp(), 
                                {1, 90000}, m_codecContext->time_base);
    
    // 填充YUV数据
    const auto* yuv_buffer = converted_frame.video_frame_buffer()->GetI420();
    av_frame->data[0] = const_cast<uint8_t*>(yuv_buffer->DataY());
    av_frame->data[1] = const_cast<uint8_t*>(yuv_buffer->DataU());
    av_frame->data[2] = const_cast<uint8_t*>(yuv_buffer->DataV());
    av_frame->linesize[0] = yuv_buffer->StrideY();
    av_frame->linesize[1] = yuv_buffer->StrideU();
    av_frame->linesize[2] = yuv_buffer->StrideV();

    // 编码并发送
    AVPacket pkt;
    av_init_packet(&pkt);
    if (avcodec_send_frame(m_codecContext, av_frame) == 0) {
        while (avcodec_receive_packet(m_codecContext, &pkt) == 0) {
            av_packet_rescale_ts(&pkt, m_codecContext->time_base, 
                               m_outputContext->streams[m_videoStreamIndex]->time_base);
            pkt.stream_index = m_videoStreamIndex;
            av_interleaved_write_frame(m_outputContext, &pkt);
            av_packet_unref(&pkt);
        }
    }
    av_frame_free(&av_frame);
}

bool CdnUploader::ConnectToCDN() {
    std::lock_guard<std::mutex> lock(m_ffmpegMutex);
    
    if (avformat_alloc_output_context2(&m_outputContext, nullptr, "flv", m_cdnUrl.c_str()) < 0) {
        RTC_LOG(LS_ERROR) << "Failed to create output context";
        return false;
    }

    // 添加视频流
    AVStream* stream = avformat_new_stream(m_outputContext, nullptr);
    avcodec_parameters_from_context(stream->codecpar, m_codecContext);
    
    if (!(m_outputContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_outputContext->pb, m_cdnUrl.c_str(), AVIO_FLAG_WRITE) < 0) {
            RTC_LOG(LS_ERROR) << "Failed to open CDN output";
            return false;
        }
    }

    if (avformat_write_header(m_outputContext, nullptr) < 0) {
        RTC_LOG(LS_ERROR) << "Failed to write stream header";
        return false;
    }

    m_videoStreamIndex = stream->index;
    m_connected = true;
    return true;
}

void CdnUploader::CleanupFFmpegResources() {
    std::lock_guard<std::mutex> lock(m_ffmpegMutex);
    
    if (m_outputContext) {
        if (m_connected) {
            av_write_trailer(m_outputContext);
        }
        if (!(m_outputContext->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_outputContext->pb);
        }
        avformat_free_context(m_outputContext);
        m_outputContext = nullptr;
    }
    
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }
}