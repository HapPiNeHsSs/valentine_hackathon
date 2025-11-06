// Linux Screen Capture via X1 (implementation)
// Beejay Urzo
// Vonage CSE 8/8/2024
#include "rtc/rtc.hpp"


#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <mutex>
#include "python_connector.h"
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/core.hpp>
#define video_resolution "640x480"
#define video_output_name "test.mp4"

static std::mutex mtx;
using std::shared_ptr;
using std::weak_ptr;
using nlohmann::json;
using namespace std::chrono_literals;
using std::shared_ptr;
using std::weak_ptr;
template <class T> weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) { return ptr; }
shared_ptr<rtc::DataChannel> dc;
std::unordered_map<std::string, shared_ptr<rtc::PeerConnection>> peerConnectionMap;
std::unordered_map<std::string, shared_ptr<rtc::DataChannel>> dataChannelMap;
std::string id;
std::string localId;
rtc::Configuration config;
constexpr int EXPECTED_RATE = 48000;
constexpr size_t EXPECTED_CHANNELS = 2;
PythonWebRTCTransport *instancePtr;
auto ws = std::make_shared<rtc::WebSocket>();
struct subscriber_context {
    std::string id;
    otc_subscriber *subscriber;
    std::string stream_id;
    std::ofstream fh;
    int64_t started = 0;
    int total_samples = 0;
};
extern std::queue<std::vector<std::byte>> GLOB_DATA_QUEUE;
PythonWebRTCTransport::PythonWebRTCTransport()
{
 
  rtc::InitLogger(rtc::LogLevel::Info);

	
	char command[256];
	std::cout << "ICE UDP mux enabled" << std::endl;
	config.enableIceUdpMux = true;

  id = "con";
  localId = "cc";
    
}



// Create and setup a PeerConnection
shared_ptr<rtc::PeerConnection> PythonWebRTCTransport::createPeerConnection(const rtc::Configuration &config,
                                                     weak_ptr<rtc::WebSocket> wws) {

  PythonWebRTCTransport *c = getInstance();																									
	std::cout << "Creating Peer Connection Inside" << std::endl;
	auto pc = std::make_shared<rtc::PeerConnection>(config);

	pc->onStateChange(
	    [](rtc::PeerConnection::State state) { std::cout << "State: " << state << std::endl; });

	pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
		std::cout << "Gathering State: " << state << std::endl;
	});	
	pc->onLocalDescription([wws, c](rtc::Description description) {
		json message = {{"id", c->id},
		                {"type", description.typeString()},
		                {"sdp", std::string(description)}};

		if (auto ws = wws.lock())
			ws->send(message.dump());
	});

	pc->onLocalCandidate([wws, c](rtc::Candidate candidate) {
		json message = {{"id", c->id},
		                {"type", "candidate"},
		                {"candidate", std::string(candidate)},
		                {"label", candidate.mid()}};

		if (auto ws = wws.lock())
			ws->send(message.dump());
	});

	pc->onDataChannel([c](shared_ptr<rtc::DataChannel> dc) {
		std::cout << "DataChannel from " << c->id << " received with label \"" << dc->label() << "\""
		          << std::endl;

		dc->onOpen([c, wdc = make_weak_ptr(dc)]() {
			if (auto dc = wdc.lock())
				 dc->send("Hello from " + c->localId);
		});

		dc->onClosed([c]() { std::cout << "DataChannel from " << c->id << " closed" << std::endl; });

		dc->onMessage([c](auto data) {
			// data holds either std::string or rtc::binary
			if (std::holds_alternative<std::string>(data))
				std::cout << "Message from " << c->id << " received: " << std::get<std::string>(data)
				          << std::endl;
			else
				std::cout << "Binary message from " << c->id
				          << " received, size=" << std::get<rtc::binary>(data).size() << std::endl;
		});
		c->dataChannelMap.emplace("audio", dc);
		c->dc = dc;
	});
	c->peerConnectionMap.emplace(c->id, pc);
	return pc;
};

inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}


PythonWebRTCTransport *PythonWebRTCTransport::getInstance()
{
  if (instancePtr == nullptr)
  {
    std::lock_guard<std::mutex> lock(mtx);
    if (instancePtr == nullptr)
    {
      instancePtr = new PythonWebRTCTransport();
    }
  }
  return instancePtr;
}

void PythonWebRTCTransport::connectWebRTC()try {

  PythonWebRTCTransport *c = getInstance();
  std::string _id = c->id;
  std::string _localId = c->localId;
  
	auto ws = std::make_shared<rtc::WebSocket>();
	std::promise<void> wsPromise;
	auto wsFuture = wsPromise.get_future();

	ws->onOpen([&wsPromise]() {
		std::cout << "WebSocket connected, signaling ready" << std::endl;	
		wsPromise.set_value();
	});

	ws->onError([&wsPromise](std::string s) {
		std::cout << "WebSocket error" << std::endl;
		wsPromise.set_exception(std::make_exception_ptr(std::runtime_error(s)));
	});

	ws->onClosed([]() { std::cout << "WebSocket closed" << std::endl; });

	ws->onMessage([c, wws = make_weak_ptr(ws)](auto data) {
		// data holds either std::string or rtc::binary
		std::cout << std::get<std::string>(data) <<std::endl;
		if (!std::holds_alternative<std::string>(data))
			return;
		std::cout <<"STEP"<<std::endl;
		json message = json::parse(std::get<std::string>(data));

		auto it = message.find("id");
		if (it == message.end())
			return;

		auto id = it->get<std::string>();

		it = message.find("type");
		if (it == message.end())
			return;
		std::cout <<"STEP2"<<std::endl;
		auto type = it->get<std::string>();

		shared_ptr<rtc::PeerConnection> pc;
		if (auto jt = c->peerConnectionMap.find(id); jt != c->peerConnectionMap.end()) {
			pc = jt->second;
		} else if (type == "offer") {
			std::cout << "Answering to " + id << std::endl;
			pc = createPeerConnection(c->config, wws);
		} else {
			return;
		}
		std::cout <<"STEP3"<<std::endl;
		if (type == "offer" || type == "answer") {
			std::cout << "SDP: " + message["sdp"].get<std::string>() << std::endl;
			auto sdp = message["sdp"].get<std::string>();
			pc->setRemoteDescription(rtc::Description(sdp, type));
			std::cout <<"STEP4.1"<<std::endl;
		} else if (type == "candidate") {
			auto sdp = message["candidate"].get<std::string>();
			auto mid = message["label"].get<std::string>();
			pc->addRemoteCandidate(rtc::Candidate(sdp, mid));
			std::cout <<"STEP4.2"<<std::endl;
		}
		std::cout <<"STEP5"<<std::endl;
	});

	const std::string wsPrefix =  "ws://";
	    // params.webSocketServer().find("://") == std::string::npos ? "ws://" : "";
	const std::string url = wsPrefix + "127.0.0.1" + ":" +
	                        "8000" + "/" + _localId;

	std::cout << "WebSocket URL is " << url << std::endl;
	ws->open(url);

	std::cout << "Waiting for signaling to be connected..." << std::endl;
	wsFuture.get();

	auto pc = createPeerConnection(c->config, ws);

		// We are the offerer, so create a data channel to initiate the process
		const std::string audio_label = "audio";
		const std::string video_label = "video";
		std::cout << "Creating Audio DataChannel with label \"" << audio_label << "\"" << std::endl;
		auto audio_dc = pc->createDataChannel(audio_label);
		std::cout << "Creating Video DataChannel with label \"" << video_label << "\"" << std::endl;
		auto video_dc = pc->createDataChannel(video_label);

		audio_dc->onOpen([c, wdc = make_weak_ptr(audio_dc)]() {
			std::cout << "Audio DataChannel from " << c->id << " open" << std::endl;
			if (auto audio_dc = wdc.lock())
			  audio_dc->send("Hello from " + c->localId);
		});

		audio_dc->onClosed([c]() { std::cout << "Audio DataChannel from " << c->id << " closed" << std::endl; });

		audio_dc->onMessage([c, wdc = make_weak_ptr(audio_dc)](auto data) {
			// data holds either std::string or rtc::binary
			if (std::holds_alternative<std::string>(data))
				std::cout << "Audio Message from " <<c->id << " received: " << std::get<std::string>(data)
				          << std::endl;
			else
				std::cout << "Audio Binary message from " << c->id
				          << " received, size=" << std::get<rtc::binary>(data).size() << std::endl;
				GLOB_DATA_QUEUE.push(std::get<rtc::binary>(data));
		});

		c->dataChannelMap.emplace("audio", audio_dc);

		video_dc->onOpen([c, wdc = make_weak_ptr(video_dc)]() {
			std::cout << "Video DataChannel from " << c->id << " open" << std::endl;
			if (auto video_dc = wdc.lock())
			  video_dc->send("Hello from " + c->localId);
		});

		video_dc->onClosed([c]() { std::cout << "Video DataChannel from " << c->id << " closed" << std::endl; });

		video_dc->onMessage([c, wdc = make_weak_ptr(video_dc)](auto data) {
			// data holds either std::string or rtc::binary
			if (std::holds_alternative<std::string>(data))
				std::cout << "Video Message from " <<c->id << " received: " << std::get<std::string>(data)
				          << std::endl;
			else
				std::cout << "Video Binary message from " << c->id
				          << " received, size=" << std::get<rtc::binary>(data).size() << std::endl;
				GLOB_DATA_QUEUE.push(std::get<rtc::binary>(data));
		});

		c->dataChannelMap.emplace("video", video_dc);

	while(true){
	 }
	std::cout << "Cleaning up..." << std::endl;

	c->dataChannelMap.clear();
	c->peerConnectionMap.clear();
	return;

} catch (const std::exception &e) {
	PythonWebRTCTransport *c = getInstance();
	std::cout << "Error: " << e.what() << std::endl;
	c->dataChannelMap.clear();
	c->peerConnectionMap.clear();
	return;
  
}

// Receive Subscriber Video Frames Here (FRAMES ARE IN YUV FORMAT)
// void PythonWebRTCTransport::on_subscriber_render_frame(otc_subscriber *subscriber,
//                                        void *user_data,
//                                        const otc_video_frame *frame)
// {

// 	PythonWebRTCTransport *c = getInstance();
// 	shared_ptr<rtc::DataChannel> dc;
// 	if (auto jt = c->dataChannelMap.find("video"); jt != c->dataChannelMap.end()) {
// 		dc = jt->second;
// 	}else {
// 		return;
// 	}
//   int width = otc_video_frame_get_width(frame);   // get width of the frame
//   int height = otc_video_frame_get_height(frame); // get height of the frame

//   uint8_t *buffer = (uint8_t *)otc_video_frame_get_buffer(frame); // get the frame buffer
//   size_t buffer_size = otc_video_frame_get_buffer_size(frame);    // get the size of the frame buffer

//   // In this section, you can send the frame to your destination of choice
// 	try{
// 		dc->send(reinterpret_cast<const std::byte *>(buffer), buffer_size);
// 	} catch (const std::exception &e) {

// 		std::cout << "Error Sending Video to Python: " << e.what() <<" Size: "<< buffer_size << std::endl;
  
// 	}

// }

// // Receive Subscriber Video Frames Here (FRAMES ARE IN YUV FORMAT)
void PythonWebRTCTransport::on_subscriber_render_frame(otc_subscriber *subscriber,
                                       void *user_data,
                                       const otc_video_frame *frame)
{
	

	std::cout << ">>>> Start Sending Video" << std::endl;
	PythonWebRTCTransport *c = getInstance();
	shared_ptr<rtc::DataChannel> dc;
	if (auto jt = c->dataChannelMap.find("video"); jt != c->dataChannelMap.end()) {
		dc = jt->second;
	}else {
		return;
	}
  int width = otc_video_frame_get_width(frame);   // get width of the frame
  int height = otc_video_frame_get_height(frame); // get height of the frame

			
	uint8_t* buffer = (uint8_t*)otc_video_frame_get_buffer(frame);
  size_t buffer_size = otc_video_frame_get_buffer_size(frame);
	std::cout << "Video Stats: " <<" Size: "<< buffer_size <<" Width: "<< width <<" Height: "<< height<< std::endl;

	
	try{
		cv::Mat y(height,width, CV_8UC1);
		cv::Mat u(height/2,width/2, CV_8UC1);
		cv::Mat v(height/2,width/2, CV_8UC1);
		const uint8_t *y_plane = otc_video_frame_get_plane_binary_data(frame, OTC_VIDEO_FRAME_PLANE_Y);
		const uint8_t *u_plane = otc_video_frame_get_plane_binary_data(frame, OTC_VIDEO_FRAME_PLANE_U);
		const uint8_t *v_plane = otc_video_frame_get_plane_binary_data(frame, OTC_VIDEO_FRAME_PLANE_V);

		memcpy(y.data, y_plane, width * height);
		memcpy(u.data, u_plane, (width/2) * (height/2));
		memcpy(v.data, v_plane, (width/2) * (height/2));

		cv::Mat u_resized, v_resized;
		cv::Size actual(width, height);
		
		cv::resize(u, u_resized, actual, 0, 0, cv::INTER_NEAREST); //repeat u values 4 times
		cv::resize(v, v_resized, actual, 0, 0, cv::INTER_NEAREST); //repeat v values 4 times

		cv::Mat yuv;
		std::vector<cv::Mat> yuv_channels = { y, u_resized, v_resized };
		cv::merge(yuv_channels, yuv);
		cv::Mat bgr;
		cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR);
		std::vector<uchar> buffer_to_send;
		std::vector<int> params;
		params.push_back(cv::IMWRITE_JPEG_QUALITY);
		params.push_back(50);
		int new_width = 600;
    int new_height = 480;
 
    // Create an empty Mat object for the resized image
    cv::Mat resized_image;
 
    // Resize the image
    cv::resize(bgr, resized_image, cv::Size(new_width, new_height));
		bool success = cv::imencode(".jpg", resized_image, buffer_to_send, params);
    if (!success) {
        // Handle error: Encoding failed
        return;
    }
		dc->send(reinterpret_cast<const std::byte *>(buffer_to_send.data()), buffer_to_send.size());
		// std::string fname = "test"+std::to_string(icounter)+".jpg";
		// cv::imwrite(fname, bgr);
		//cv::Mat img = yuv420ToMat( const_cast<unsigned char*>(otc_video_frame_get_buffer(frame)), width, height);
		// cv::Mat i420Image(height * 3 / 2, width, CV_8UC1, const_cast<unsigned char*>(buffer));
		// cv::Mat bgrImage;
		// cv::cvtColor(i420Image, bgrImage, cv::COLOR_YUV2BGR_I420);
		
		
		// bool result = cv::imencode(".jpg", img, out_buffer, params);
		// dc->send(reinterpret_cast<const std::byte *>(out_buffer.data()), out_buffer.size());
		} catch (const std::exception &e) {

			std::cout << "Error Sending Video to Python: " << e.what() <<" Size: "<< buffer_size << std::endl;
		
	}
	

	// std::string video_mark = "{\"vid_size\":"+std::to_string(buffer_size)+ "}";
  // In this section, you can send the frame to your destination of choice
	// dc->send(video_mark);
	// for(size_t start = 0; start>=buffer_size; start+=chunk){
	// 	if(start + chunk > buffer_size){
	// 		chunk = buffer_size - start;
	// 	}
	// 	try{
	// 		dc->send(reinterpret_cast<const std::byte *>(&buffer[start]), chunk);
	// 	} catch (const std::exception &e) {

	// 		std::cout << "Error Sending Video to Python: " << e.what() <<" Size: "<< buffer_size << std::endl;
		
	// 	}
	// }
	

}

void PythonWebRTCTransport::on_subscriber_audio(otc_subscriber *subscriber,
                                         void *user_data,
                                         const struct otc_audio_data *audio_data)
{
  PythonWebRTCTransport *c = getInstance();
	// std::cout << "Subscriber Audio" << std::endl;
	auto *ctx = static_cast<struct subscriber_context *>(user_data);
  // We are generally unopinionated about the sample rate, however, the fluctuations that can occur
	// in response to network behavior interfer with our test and we prefer to not deal w/ resampling
	// right now. Generally this only happens at the start of the stream.
	shared_ptr<rtc::DataChannel> dc;
	if (auto jt = c->dataChannelMap.find("audio"); jt != c->dataChannelMap.end()) {
		dc = jt->second;
	}else {
		return;
	}

	// shared_ptr<rtc::PeerConnection> pc;
	// if (auto jt = c->peerConnectionMap.find(c->id); jt != c->peerConnectionMap.end()) {
	// 	pc = jt->second;
	// }else {
	// 	return;
	// }

	rtc::Description::Audio media("audio", rtc::Description::Direction::SendRecv);

	if (audio_data->sample_rate != EXPECTED_RATE || audio_data->number_of_channels != EXPECTED_CHANNELS) {
			printf("[%s] DROPPING unexpected frame: sample rate %dHz / %ld channels\n",
							ctx->id.c_str(),
							audio_data->sample_rate,
							audio_data->number_of_channels);
			return;
	}

	if (ctx->started == 0) {
			ctx->started = now_ms();
	}


	size_t bytes = audio_data->number_of_samples  * (audio_data->bits_per_sample >> 3)*audio_data->number_of_channels;
	auto seconds = (now_ms() - ctx->started) / 1000.0;

	// printf(">>[%s] frame: %ld samples, %dHz, %ld channels; %d total samples, %.2fs, %d samples/second, %.2ld bytes\n",
	// 				ctx->id.c_str(),
	// 				audio_data->number_of_samples,
	// 				audio_data->sample_rate,
	// 				audio_data->number_of_channels,
	// 				ctx->total_samples,
	// 				seconds,
	// 				static_cast<int>(ctx->total_samples / seconds),
	// 				bytes);

	ctx->total_samples += audio_data->number_of_samples;
	try{
		dc->send(reinterpret_cast<const std::byte *>(audio_data->sample_buffer), bytes);
	} catch (const std::exception &e) {

		std::cout << "Error Sending Audio to Python: " << e.what() << std::endl;
  
	}
	
}