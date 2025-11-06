#include <fstream>
#include <iostream>
#include <map>
#include "python_connector.h"
#include <opentok.h>
#include "config.h"
#include <uv.h>
#include <atomic>
#include <chrono>
#include "otk_thread.h"
#include "renderer.h"
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/core.hpp>
#include <thread>

using namespace cv;
PythonWebRTCTransport *PythonWebRTCTransport ::instancePtr = new PythonWebRTCTransport();
std::mutex PythonWebRTCTransport::mtx;
typedef unsigned char byte;
constexpr otc_log_level OTC_LOGLEVEL = OTC_LOG_LEVEL_WARN;

// Our anticipated stream config
constexpr int EXPECTED_RATE = 48000;
constexpr size_t EXPECTED_CHANNELS = 2;
std::queue<std::vector<std::byte>> GLOB_DATA_QUEUE;
int counter = 0;
#define SAMPLE_RATE 48000
#define NUM_CHANNELS 2
#define FRAME_SIZE_MS 10
#define NUM_SAMPLES_PER_FRAME (SAMPLE_RATE / 1000 * FRAME_SIZE_MS)

static std::atomic<bool> g_is_connected(false);
static otc_publisher *g_publisher = nullptr;
static std::atomic<bool> g_is_publishing(false);
extern void default_otc_logger_callback(const char *message);
extern struct otc_audio_device_callbacks default_device_callbacks;
extern struct otc_session_callbacks default_session_callbacks;
extern struct otc_subscriber_callbacks default_subscriber_callbacks;
extern struct otc_publisher_callbacks default_publisher_callbacks;

struct subscriber_context {
    std::string id;
    otc_subscriber *subscriber;
    std::string stream_id;
    std::ofstream fh;
    int64_t started = 0;
    int total_samples = 0;
    //RendererManager *render_manager;
};


uv_loop_t *loop;
uv_signal_t sig_int;

std::map<std::string, subscriber_context *> subscribers;
std::string g_outputDir;

inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}


struct audio_device {
  otc_audio_device_callbacks audio_device_callbacks;
  uv_thread_t capturer_thread;
  uv_thread_t renderer_thread;
  std::atomic<bool> capturer_thread_exit;
  std::atomic<bool> exiting;
};

static otk_thread_func_return_type capturer_thread_start_function(void *arg) {
  struct audio_device *device = static_cast<struct audio_device *>(arg);
  if (device == nullptr) {
    otk_thread_func_return_value;
  }
  otc_audio_device_write_capture_data(0, 480);
  static double time = 0;
  int16_t samples[NUM_SAMPLES_PER_FRAME * NUM_CHANNELS];
  while (device->capturer_thread_exit.load() == false) {
    if(!GLOB_DATA_QUEUE.empty()){
      std::vector<std::byte> v = GLOB_DATA_QUEUE.front();
      size_t actual = v.size();
      std::cout << "Data In " <<actual << std::endl;
      
      GLOB_DATA_QUEUE.pop();
      memcpy(samples,&v[0], actual);
      otc_audio_device_write_capture_data(samples, 480);
    }
    else{
      continue;
      // std::cout << "Silence" << std::endl;

    }
    //free(samples);
    usleep(10 * 1000);
  }  

  otk_thread_func_return_value;
}

static otc_bool audio_device_destroy_capturer(const otc_audio_device *audio_device,
                                              void *user_data) {
  struct audio_device *device = static_cast<struct audio_device *>(user_data);
  if (device == nullptr) {
    return OTC_FALSE;
  }

  device->capturer_thread_exit = true;
  otk_thread_join(device->capturer_thread);

  return OTC_TRUE;
}

static otc_bool audio_device_start_capturer(const otc_audio_device *audio_device,
                                            void *user_data) {
  struct audio_device *device = static_cast<struct audio_device *>(user_data);
  if (device == nullptr) {
    return OTC_FALSE;
  }

  device->capturer_thread_exit = false;
  if (otk_thread_create(&(device->capturer_thread), &capturer_thread_start_function, (void *)device) != 0) {
    return OTC_FALSE;
  }

  return OTC_TRUE;
}

static otc_bool audio_device_get_capture_settings(const otc_audio_device *audio_device,
                                                  void *user_data,
                                                  struct otc_audio_device_settings *settings) {
  if (settings == nullptr) {
    return OTC_FALSE;
  }

  settings->number_of_channels = NUM_CHANNELS;
  settings->sampling_rate = SAMPLE_RATE;
  return OTC_TRUE;
}

static void *device_renderer_thread_fn(void *arg) {
    std::cout << "[audio_device.device_renderer_thread_fn]" << std::endl;

    struct audio_device *device = static_cast<struct audio_device *>(arg);

    std::ofstream fh(g_outputDir + "/device.pcm", std::ios::trunc | std::ios::binary);

    while (device->exiting.load() == false) {
        int16_t samples[NUM_SAMPLES_PER_FRAME * NUM_CHANNELS];

        size_t actual = otc_audio_device_read_render_data(samples, NUM_SAMPLES_PER_FRAME);

        // std::cout << "Read samples: " << actual << " writing bytes: " << actual * NUM_CHANNELS * sizeof(int16_t) << std::endl;

        if (actual > 0) {
            fh.write(reinterpret_cast<const char *>(samples), actual * NUM_CHANNELS * sizeof(int16_t));
        }

        usleep(FRAME_SIZE_MS * 1000);
    }

    fh.close();

    std::cout << "[audio_device.device_renderer_thread_fn] exiting" << std::endl;

    return nullptr;
}

static otc_bool audio_device_start_renderer(const otc_audio_device *audio_device, void *user_data) {
    std::cout << "[audio_device.start_renderer]" << std::endl;

    struct audio_device *device = static_cast<struct audio_device *>(user_data);
    device->exiting = false;

    if (pthread_create(&(device->renderer_thread), NULL, &device_renderer_thread_fn, device) != 0) {
        return OTC_FALSE;
    }

    return OTC_TRUE;
}

static otc_bool audio_device_destroy_renderer(const otc_audio_device *audio_device, void *user_data) {
    std::cout << "[audio_device.destroy_renderer]" << std::endl;
    struct audio_device *device = static_cast<struct audio_device *>(user_data);

    device->exiting = true;
    pthread_join(device->renderer_thread, NULL);

    return OTC_TRUE;
}

static otc_bool get_render_settings(const otc_audio_device *audio_device, void *user_data, struct otc_audio_device_settings *settings) {
    std::cout << "[audio_device.get_render_settings]" << std::endl;
    if (settings == nullptr)
        return OTC_FALSE;

    settings->number_of_channels = NUM_CHANNELS;
    settings->sampling_rate = SAMPLE_RATE;

    return OTC_TRUE;
}

struct custom_video_capturer {
  const otc_video_capturer *video_capturer;
  struct otc_video_capturer_callbacks video_capturer_callbacks;
  int width;
  int height;
  otk_thread_t capturer_thread;
  std::atomic<bool> capturer_thread_exit;
};

static int generate_random_integer() {
  srand(time(nullptr));
  return rand();
}


static otk_thread_func_return_type xvideo_capturer_thread_start_function(void *arg) {  
  const int frameTime = 33;

  // Start time of the current frame
  auto start = std::chrono::steady_clock::now();

  struct custom_video_capturer *video_capturer = static_cast<struct custom_video_capturer *>(arg);
  if (video_capturer == nullptr)
  {
    otk_thread_func_return_value;
  }
  byte *buffer = (byte *)malloc(sizeof(byte) * video_capturer->width * video_capturer->height * 4);
  std::string filename = "../speak_video.mp4";
  std::cout << "Start" << std::endl;
  cv::VideoCapture cap(filename, cv::CAP_ANY);
  std::cout << "Next" << std::endl;
  int height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
  int width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
  if (!cap.isOpened())
  {
    std::cout << "Error opening video stream or file" << std::endl;
    memset(buffer, generate_random_integer() & 0xFF, video_capturer->width * video_capturer->height * 4);
    otc_video_frame *otc_frame = otc_video_frame_new(OTC_VIDEO_FRAME_FORMAT_YUV420P, video_capturer->width, video_capturer->height, buffer);
    // otc_video_frame *otc_frame = otc_video_frame_new(OTC_VIDEO_FRAME_FORMAT_YUV420P, width, height, frame.data);
    otc_video_capturer_provide_frame(video_capturer->video_capturer, 0, otc_frame);
    if (otc_frame != nullptr)
    {
      otc_video_frame_delete(otc_frame);
    }
  }
  std::cout << "Render Start" <<height <<" "<<width<<" "<< std::endl;
  while (1)
  {
    
    Mat frame;
    // Capture frame-by-frame
    cap >> frame;
    // If the frame is empty, break immediately
    if(GLOB_DATA_QUEUE.empty()){
       cap.set(cv::CAP_PROP_POS_FRAMES, 0);
    }

    if (frame.empty()){
      cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            continue;
      // std::cout << "Frame Empty" << std::endl;
      // cap.release();
      // return 0;
      // otk_thread_func_return_value;
      
    }else{
      // std::cout << "Render" << std::endl;
      cv::Mat image = cv::Mat(height, width, CV_8UC3);
      cv::cvtColor(frame, image, cv::COLOR_BGR2YUV_I420);

      // // Display the resulting frame
      otc_video_frame *otc_frame = otc_video_frame_new(OTC_VIDEO_FRAME_FORMAT_YUV420P, width, height, image.data);
      otc_video_capturer_provide_frame(video_capturer->video_capturer, 0, otc_frame);
      if (otc_frame != nullptr)
      {
        otc_video_frame_delete(otc_frame);
      }

    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Calculate the time remaining before the next frame
    auto remaining = frameTime - elapsed.count();

    // If the frame rendering was faster than the target frame time, wait for the remaining time
    if (remaining > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(remaining));
    }

    // Update the start time for the next frame
    start = std::chrono::steady_clock::now();

    // // Print a message to indicate a frame has been rendered
    // std::cout << "Frame rendered" << std::endl;       
  }

  // When everything done, release the video capture object
  cap.release();

  // Closes all the frames

  return 0;
  otk_thread_func_return_value;
}


static otc_bool video_capturer_init(const otc_video_capturer *capturer, void *user_data) {
  struct custom_video_capturer *video_capturer = static_cast<struct custom_video_capturer *>(user_data);
  if (video_capturer == nullptr) {
    return OTC_FALSE;
  }

  video_capturer->video_capturer = capturer;

  return OTC_TRUE;
}

static otc_bool video_capturer_destroy(const otc_video_capturer *capturer, void *user_data) {
  struct custom_video_capturer *video_capturer = static_cast<struct custom_video_capturer *>(user_data);
  if (video_capturer == nullptr) {
    return OTC_FALSE;
  }

  video_capturer->capturer_thread_exit = true;
  otk_thread_join(video_capturer->capturer_thread);

  return OTC_TRUE;
}

static otc_bool video_capturer_start(const otc_video_capturer *capturer, void *user_data) {
  std::cout << "STARTING CAPTURE" << std::endl;
  struct custom_video_capturer *video_capturer = static_cast<struct custom_video_capturer *>(user_data);
  if (video_capturer == nullptr) {
    return OTC_FALSE;
  }

  video_capturer->capturer_thread_exit = false;
  if (otk_thread_create(&(video_capturer->capturer_thread), &xvideo_capturer_thread_start_function, (void *)video_capturer) != 0) {
    return OTC_FALSE;
  }

  return OTC_TRUE;
}

static otc_bool get_video_capturer_capture_settings(const otc_video_capturer *capturer,
                                                    void *user_data,
                                                    struct otc_video_capturer_settings *settings) {
  struct custom_video_capturer *video_capturer = static_cast<struct custom_video_capturer *>(user_data);
  if (video_capturer == nullptr) {
    return OTC_FALSE;
  }

  settings->format = OTC_VIDEO_FRAME_FORMAT_ARGB32;
  settings->width = video_capturer->width;
  settings->height = video_capturer->height;
  settings->fps = 30;
  settings->mirror_on_local_render = OTC_FALSE;
  settings->expected_delay = 0;

  return OTC_TRUE;
}

static void on_subscriber_connected(otc_subscriber *subscriber, void *user_data, const otc_stream *stream) {
    auto *ctx = static_cast<struct subscriber_context *>(user_data);
    std::cout << "[subscriber.on_connected] " << ctx->id << std::endl;

    otc_subscriber_set_subscribe_to_video(subscriber, OTC_TRUE);
    otc_subscriber_set_subscribe_to_audio(subscriber, OTC_TRUE);
    std::cout << "[subscriber.on_connected and subscribed] " << ctx->id << std::endl;
    ctx->fh.open(g_outputDir + "/" + ctx->id + ".pcm", std::ios::binary);

}

static void on_subscriber_disconnected(otc_subscriber *subscriber, void *user_data) {
    auto *ctx = static_cast<struct subscriber_context *>(user_data);
    // std::cout << "[subscriber.on_disconnected] " << ctx->id << std::endl;

    // if (ctx->fh.is_open()) {
    //     ctx->fh.flush();
    // }
}


static void on_subscriber_render_frame(otc_subscriber *subscriber,
                                       void *user_data,
                                       const otc_video_frame *frame) {
  //RendererManager *render_manager = static_cast<RendererManager*>(static_cast<subscriber_context*>(user_data)->render_manager);
  // if (render_manager == nullptr) {
  //   std::cout << "No Renderer" << std::endl;
  //   return;
  // }
  int width = otc_video_frame_get_width(frame);   // get width of the frame
  int height = otc_video_frame_get_height(frame); // get height of the frame

			
	uint8_t* buffer = (uint8_t*)otc_video_frame_get_buffer(frame);
  size_t buffer_size = otc_video_frame_get_buffer_size(frame);


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
		std::string fname = "test"+std::to_string(counter)+".jpg";
		cv::imwrite(fname, bgr);
		//cv::Mat img = yuv420ToMat( const_cast<unsigned char*>(otc_video_frame_get_buffer(frame)), width, height);
		// cv::Mat i420Image(height * 3 / 2, width, CV_8UC1, const_cast<unsigned char*>(buffer));
		// cv::Mat bgrImage;
		// cv::cvtColor(i420Image, bgrImage, cv::COLOR_YUV2BGR_I420);
		
		
		counter++;
		
		// bool result = cv::imencode(".jpg", img, out_buffer, params);
		// dc->send(reinterpret_cast<const std::byte *>(out_buffer.data()), out_buffer.size());
		} catch (const std::exception &e) {

			std::cout << "Error Sending Video to Python: " << e.what() <<" Size: "<< buffer_size << std::endl;
		
	}

  
 // render_manager->addFrame(subscriber, frame);
}

static void on_subscriber_error(otc_subscriber* subscriber,
                                void *user_data,
                                const char* error_string,
                                enum otc_subscriber_error_code error) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "Subscriber error. Error code: " << error_string << std::endl;
}

static void on_subscriber_audio_data(otc_subscriber *subscriber,
                                     void *user_data,
                                     const struct otc_audio_data *audio_data) {
    auto *ctx = static_cast<struct subscriber_context *>(user_data);

    // We are generally unopinionated about the sample rate, however, the fluctuations that can occur
    // in response to network behavior interfer with our test and we prefer to not deal w/ resampling
    // right now. Generally this only happens at the start of the stream.
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

    printf("[%s] frame: %ld samples, %dHz, %ld channels; %d total samples, %.2fs, %d samples/second, %.2ld bytes\n",
           ctx->id.c_str(),
           audio_data->number_of_samples,
           audio_data->sample_rate,
           audio_data->number_of_channels,
           ctx->total_samples,
           seconds,
           static_cast<int>(ctx->total_samples / seconds),
           bytes);

    ctx->total_samples += audio_data->number_of_samples;
    ctx->fh.write(reinterpret_cast<const char *>(audio_data->sample_buffer), bytes);

}

static void on_session_connected(otc_session *session, void *user_data) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;

  g_is_connected = true;

  if ((session != nullptr) && (g_publisher != nullptr)) {
    if (otc_session_publish(session, g_publisher) == OTC_SUCCESS) {
      g_is_publishing = true;
      return;
    }
    std::cout << "Could not publish successfully" << std::endl;
  }
}

// static void on_session_stream_received(otc_session *session,
//                                        void *user_data,
//                                        const otc_stream *stream) {
//   std::cout << __FUNCTION__ << " callback function" << std::endl;
// }

static void on_session_stream_received(otc_session *session,
                                       void *user_data,
                                       const otc_stream *stream) {
    //RendererManager *render_manager = static_cast<RendererManager*>(user_data);
    // if (render_manager == nullptr) {
    //   return;
    // }
    const char *stream_id = otc_stream_get_id(stream);
    std::cout << "[session.on_stream_received]: " << stream_id << std::endl;

    struct subscriber_context *ctx = new struct subscriber_context();
    ctx->stream_id = stream_id;
    //ctx->render_manager = static_cast<RendererManager*>(user_data);
    struct otc_subscriber_callbacks subscriber_callbacks = default_subscriber_callbacks;
    PythonWebRTCTransport *pythonCon = PythonWebRTCTransport::getInstance();
    subscriber_callbacks.user_data = static_cast<void *>(ctx);
    subscriber_callbacks.on_connected = on_subscriber_connected;
    subscriber_callbacks.on_disconnected = on_subscriber_disconnected;
    subscriber_callbacks.on_audio_data = pythonCon->on_subscriber_audio;
    subscriber_callbacks.on_render_frame = pythonCon->on_subscriber_render_frame;
    // subscriber_callbacks.on_audio_data = on_subscriber_audio_data;
    //subscriber_callbacks.on_render_frame = on_subscriber_render_frame;
    // subscriber_callbacks.on_audio_data =  on_subscriber_audio;

    // struct otc_subscriber_callbacks subscriber_callbacks = {0};
    // // subscriber_callbacks.user_data = static_cast<void *>(ctx);
    // subscriber_callbacks.user_data = user_data;
    // subscriber_callbacks.on_connected = on_subscriber_connected;
    // // subscriber_callbacks.on_render_frame = pythonCon->on_subscriber_render_frame;
    // 
    // subscriber_callbacks.on_error = on_subscriber_error;

    otc_subscriber *subscriber = otc_subscriber_new(stream,
                                                  &subscriber_callbacks);

    
    //render_manager->createRenderer(subscriber);
    if (subscriber == nullptr) {
        std::cerr << "[session.on_stream_received]: " << stream_id << " otc_subscriber_new() failed" << std::endl;
        delete ctx;
        return;
    }
    if (otc_session_subscribe(session, subscriber) != OTC_SUCCESS) {
        std::cerr << "[session.on_stream_received]: " << stream_id << " otc_session_subscribe() failed" << std::endl;
        delete ctx;
        return;
    }

    ctx->subscriber = subscriber;
    ctx->id = otc_subscriber_get_subscriber_id(subscriber);
    subscribers[ctx->id] = ctx;
    std::cout << "[session.on_stream_received]: " << ctx->id << " SUBSCRIBED" << std::endl;
}

static void on_session_connection_created(otc_session *session,
                                          void *user_data,
                                          const otc_connection *connection) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
}

static void on_session_connection_dropped(otc_session *session,
                                          void *user_data,
                                          const otc_connection *connection) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
}

static void on_session_stream_dropped(otc_session *session,
                                      void *user_data,
                                      const otc_stream *stream) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
}

static void on_session_disconnected(otc_session *session, void *user_data) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
}

static void on_session_error(otc_session *session,
                             void *user_data,
                             const char *error_string,
                             enum otc_session_error_code error) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "Session error. Error : " << error_string << std::endl;
}

void abort(uv_signal_t *handle, int signum) {
    uv_signal_stop(handle);
    uv_stop(loop);
}

static void on_publisher_stream_created(otc_publisher *publisher,
                                        void *user_data,
                                        const otc_stream *stream) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
}

static void on_publisher_render_frame(otc_publisher *publisher,
                                      void *user_data,
                                      const otc_video_frame *frame) {
  //std::cout << __FUNCTION__ << " callback function" << std::endl;                                        
  // RendererManager *render_manager = static_cast<RendererManager*>(user_data);
  // if (render_manager == nullptr) {
  //   return;
  // }
  // render_manager->addFrame(publisher, frame);
}

static void on_publisher_stream_destroyed(otc_publisher *publisher,
                                          void *user_data,
                                          const otc_stream *stream) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
}

static void on_publisher_error(otc_publisher *publisher,
                               void *user_data,
                               const char* error_string,
                               enum otc_publisher_error_code error_code) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "Publisher error. Error code: " << error_string << std::endl;
}

static void on_otc_log_message(const char* message) {
  std::cout <<  __FUNCTION__ << ":" << message << std::endl;
}


int main(int argc, char **argv) {
    PythonWebRTCTransport *pythonCon = PythonWebRTCTransport::getInstance();
    // //put the websocket into it's own thread
    std::thread t{ PythonWebRTCTransport::connectWebRTC };

    //RendererManager renderer_manager;
    g_outputDir = "./";
    const char *apiKey ;
    const char *sessionId ;
    const char *token ;
    if (argc == 1)
    {
      std::cout << "Using 'config.h' for credentials" << std::endl;
      apiKey = API_KEY;
      sessionId = SESSION_ID;
      token = TOKEN;
    }
    else if (argc == 4)
    {
      std::cout << "Using parameters for credentials" << std::endl;
      apiKey = argv[1];
      sessionId = argv[2];
      token = argv[3];
    }
    else if (argc < 4)
    {
      std::cerr << "Usage: " << argv[0] << "<apiKey> <sessionId> <token>" << std::endl;
      return EXIT_FAILURE;
    }

    // std::cout << "OUTPUT DIR: " << g_outputDir << std::endl;
    std::cout << "API KEY: " << apiKey << std::endl;
    std::cout << "SESSION ID: " << sessionId << std::endl;
    std::cout << "TOKEN: " << token << std::endl;

    if (otc_init(nullptr) != OTC_SUCCESS) {
        std::cerr << "otc_init() failed" << std::endl;
        return EXIT_FAILURE;
    }

    otc_log_set_logger_callback(default_otc_logger_callback);
    otc_log_enable(OTC_LOGLEVEL);

    struct audio_device *device = (struct audio_device *)malloc(sizeof(struct audio_device));

    device->audio_device_callbacks = {0};
    device->audio_device_callbacks.get_render_settings = get_render_settings;
    device->audio_device_callbacks.start_renderer = audio_device_start_renderer;
    device->audio_device_callbacks.destroy_renderer = audio_device_destroy_renderer;
    device->audio_device_callbacks.destroy_capturer = audio_device_destroy_capturer;
    device->audio_device_callbacks.start_capturer = audio_device_start_capturer;
    device->audio_device_callbacks.get_capture_settings = audio_device_get_capture_settings;
    device->audio_device_callbacks.user_data = static_cast<void *>(device);

    otc_set_audio_device(&(device->audio_device_callbacks));    
    struct otc_session_callbacks session_callbacks = {0};
    session_callbacks.on_connected = on_session_connected;
    session_callbacks.user_data =  {0};//&renderer_manager;
    session_callbacks.on_connection_created = on_session_connection_created;
    session_callbacks.on_connection_dropped = on_session_connection_dropped;
    session_callbacks.on_stream_received = on_session_stream_received;
    session_callbacks.on_stream_dropped = on_session_stream_dropped;
    session_callbacks.on_disconnected = on_session_disconnected;
    session_callbacks.on_error = on_session_error;

    otc_session *session = nullptr;
    session = otc_session_new(apiKey, sessionId, &session_callbacks);

    if (session == nullptr) {
        std::cerr << "otc_session_new() failed" << std::endl;
        return EXIT_FAILURE;
    }
    struct custom_video_capturer *video_capturer = (struct custom_video_capturer *)malloc(sizeof(struct custom_video_capturer));
    video_capturer->video_capturer_callbacks = {0};
    video_capturer->video_capturer_callbacks.user_data = static_cast<void *>(video_capturer);
    video_capturer->video_capturer_callbacks.init = video_capturer_init;
    video_capturer->video_capturer_callbacks.destroy = video_capturer_destroy;
    video_capturer->video_capturer_callbacks.start = video_capturer_start;
    video_capturer->video_capturer_callbacks.get_capture_settings = get_video_capturer_capture_settings;
    video_capturer->width = 640;
    video_capturer->height = 480;

    // RendererManager renderer_manager;
    struct otc_publisher_callbacks publisher_callbacks = {0};
    publisher_callbacks.user_data =  {0}; //&renderer_manager;
    publisher_callbacks.on_stream_created = on_publisher_stream_created;
    publisher_callbacks.on_render_frame = on_publisher_render_frame;
    publisher_callbacks.on_stream_destroyed = on_publisher_stream_destroyed;
    publisher_callbacks.on_error = on_publisher_error;

    g_publisher = otc_publisher_new("opentok-linux-sdk-samples",
                                    &(video_capturer->video_capturer_callbacks),
                                    &publisher_callbacks);

    if (g_publisher == nullptr) {
        std::cout << "Could not create OpenTok publisher successfully" << std::endl;
        otc_session_delete(session);
        return EXIT_FAILURE;
    }
    //renderer_manager.createRenderer(g_publisher);
    otc_session_connect(session, token);

    loop = uv_default_loop();
    uv_signal_init(loop, &sig_int);
    uv_signal_start(&sig_int, abort, SIGINT);
    uv_run(loop, UV_RUN_DEFAULT);

    // std::cout << "Aborting due to SIGINT" << std::endl;
    // uv_loop_close(loop);

    // otc_session_disconnect(session);
    // otc_session_delete(session);
    // otc_destroy();

    // renderer_manager.runEventLoop();

    // renderer_manager.destroyRenderer(g_publisher);

    if ((session != nullptr) && (g_publisher != nullptr) && g_is_publishing.load()) {
        otc_session_unpublish(session, g_publisher);
    }

    
     if (g_publisher != nullptr) {
    otc_publisher_delete(g_publisher);
    }

    if ((session != nullptr) && g_is_connected.load()) {
        otc_session_disconnect(session);
    }

    if (session != nullptr) {
        otc_session_delete(session);
    }

    // if (video_capturer != nullptr) {
    //     free(video_capturer);
    // }

    otc_destroy();

    // for (auto &pair : subscribers) {
    //     struct subscriber_context *ctx = pair.second;

    //     if (ctx->fh.is_open()) {
    //         ctx->fh.close();
    //     }

    //     otc_subscriber_delete(ctx->subscriber);
    //     delete ctx;
    // }

    //subscribers.clear();

    return EXIT_SUCCESS;
}