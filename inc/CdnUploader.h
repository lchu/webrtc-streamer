#include <string>
#include <mutex>
#include <regex>
#include <thread>
#include <future>
#include <atomic>

#include "api/peer_connection_interface.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "p2p/client/basic_port_allocator.h"
#include "modules/audio_device/include/audio_device.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/json.h"


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