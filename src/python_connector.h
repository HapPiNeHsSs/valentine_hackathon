// Linux Screen Capture via X1 (implementation)
// Beejay Urzo
// Vonage CSE 8/8/2024

#include <opentok.h>
#include <iostream>
#include <mutex>
#include "rtc/rtc.hpp"

#include <nlohmann/json.hpp>
using std::shared_ptr;
using std::weak_ptr;

class PythonWebRTCTransport
{
private:
    // Static pointer to the Singleton instance
    static PythonWebRTCTransport *instancePtr;
    std::string id;
    std::string localId;
    FILE *video_out_file;
    // Mutex to ensure thread safety
    //shared_ptr<rtc::WebSocket> ws;
    static shared_ptr<rtc::PeerConnection> createPeerConnection(const rtc::Configuration &config,weak_ptr<rtc::WebSocket> wws);
    // Private Constructor
    PythonWebRTCTransport();
    std::unordered_map<std::string, shared_ptr<rtc::PeerConnection>> peerConnectionMap;
    std::unordered_map<std::string, shared_ptr<rtc::DataChannel>> dataChannelMap;
    rtc::Configuration config;
    shared_ptr<rtc::DataChannel> dc;
public:
    // Deleting the copy constructor to prevent copies
    PythonWebRTCTransport(const PythonWebRTCTransport &obj) = delete;
    static void connectWebRTC();    
    static std::mutex mtx;
    // Static method to get the Singleton instance
    static PythonWebRTCTransport *getInstance();
    static void on_subscriber_audio(otc_subscriber *subscriber,
                                    void *user_data,
                                    const struct otc_audio_data *audio_data);
    static void on_subscriber_render_frame(otc_subscriber *subscriber,
                                       void *user_data,
                                       const otc_video_frame *frame);                               
};