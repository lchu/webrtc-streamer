#include <mutex>
#include <atomic>
extern "C" {
#include <avcodec.h>
#include <avformat.h>
}

class CdnUploader : public rtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
    CdnUploader(const Json::Value & config);
    ~CdnUploader();

    // 视频处理接口
    void OnFrame(const webrtc::VideoFrame& frame) override;

    // 状态检查
    bool IsConnected() const { return m_connected.load(); }

private:
    void InitFFmpegResources();
    void CleanupFFmpegResources();
    bool ConnectToCDN();
    // 配置加载
    bool LoadConfig(const Json::Value & config);

    // FFmpeg资源
    AVFormatContext* m_outputContext = nullptr;
    AVCodecContext* m_codecContext = nullptr;
    int m_videoStreamIndex = -1;
    
    // 状态控制
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_running{false};
    std::mutex m_ffmpegMutex;
    
    // 配置参数
    std::string m_cdnUrl;
    int m_bitrate = 2000000;
    int m_fps = 30;
    int m_width = 1280;
    int m_height = 720;
};