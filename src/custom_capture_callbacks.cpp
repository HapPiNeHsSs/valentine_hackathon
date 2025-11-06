//Video Capture Callbacks that can Capture the screen
//Beejay Urzo
//Vonage CSE 8/8/2024

#include <iostream>
#include <opentok.h>
#include "otk_thread.h"
#include <atomic>
// #include "screen_capture.h"

typedef unsigned char byte;
int screen_cap; //this will determine if we are in screen cap mode or camera mode. 1 = Screen Share, 2 = Camera, 0 = quit

struct custom_capturer
{
  const otc_video_capturer *video_capturer;
  struct otc_video_capturer_callbacks video_capturer_callbacks;
  int width;
  int height;
  otk_thread_t capturer_thread;
  std::atomic<bool> capturer_thread_exit;
};


static int generate_random_integer()
{
  srand(time(nullptr));
  return rand();
}

static otk_thread_func_return_type capturer_thread_start_function(void *arg)
{
  struct custom_capturer *video_capturer = static_cast<struct custom_capturer *>(arg);
  if (video_capturer == nullptr)
  {
    otk_thread_func_return_value;
  }

//   cv::VideoCapture cap(0);
//   int cap_height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
//   int cap_width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
//   ScreenShot* screen = new ScreenShot();
//   // Capture frame-by-frame
//   XImage *xImage;
//   cv::Mat frame = cv::Mat(cap_height, cap_width, CV_8UC4);
//   while (1)
//   {
//     if(screen_cap == 1){ //use screencapture
//         xImage = screen->get_screenshot_pixel();
//         // // Display the resulting frame
//         otc_video_frame *otc_frame = otc_video_frame_new(OTC_VIDEO_FRAME_FORMAT_ARGB32, xImage->width, xImage->height, (u_int8_t *)xImage->data);
//         otc_video_capturer_provide_frame(video_capturer->video_capturer, 0, otc_frame);
//         if (otc_frame != nullptr)
//         {
//             otc_video_frame_delete(otc_frame);
//         }
//     }else if(screen_cap == 2){ //use camera
//         cap >> frame;
//         cv::Mat image = cv::Mat(cap_height, cap_width, CV_8UC3);
//         cv::cvtColor(frame, image, cv::COLOR_BGR2YUV_I420);
//         otc_video_frame *otc_frame = otc_video_frame_new(OTC_VIDEO_FRAME_FORMAT_YUV420P, cap_width, cap_height, image.data);
//         // otc_video_frame *otc_frame = otc_video_frame_new(OTC_VIDEO_FRAME_FORMAT_ARGB32, cap_width, cap_height, frame.data);
//         otc_video_capturer_provide_frame(video_capturer->video_capturer, 0, otc_frame);
//         if (otc_frame != nullptr)
//         {
//             otc_video_frame_delete(otc_frame);            
//         }
//     }
//     else if (screen_cap == 0){ //ends rendering if code is 0
//         cap.~VideoCapture();
//         screen->~ScreenShot();
//         break;
//     }
//     usleep(1000);
//   }
  return 0;
  otk_thread_func_return_value;
}

struct otc_video_capturer_callbacks custom_capture_callbacks = {
    .init = [](const otc_video_capturer *capturer, void *user_data) -> otc_bool{

        struct custom_capturer *video_capturer = static_cast<struct custom_capturer *>(user_data);
        if (video_capturer == nullptr)
        {
            return OTC_FALSE;
        }

        video_capturer->video_capturer = capturer;

        return OTC_TRUE;
    },
    .destroy = [](const otc_video_capturer *capturer, void *user_data) -> otc_bool {
        struct custom_capturer *video_capturer = static_cast<struct custom_capturer *>(user_data);
        if (video_capturer == nullptr)
        {
            return OTC_FALSE;
        }

        video_capturer->capturer_thread_exit = true;
        otk_thread_join(video_capturer->capturer_thread);

        return OTC_TRUE;
    },
    .start = [](const otc_video_capturer *capturer, void *user_data) -> otc_bool{
        struct custom_capturer *video_capturer = static_cast<struct custom_capturer *>(user_data);
        if (video_capturer == nullptr)
        {
            return OTC_FALSE;
        }

        video_capturer->capturer_thread_exit = false;
        if (otk_thread_create(&(video_capturer->capturer_thread), &capturer_thread_start_function, (void *)video_capturer) != 0)
        {
            return OTC_FALSE;
        }

        return OTC_TRUE;

    },
    .stop = [](const otc_video_capturer *capturer, void *user_data) -> otc_bool{
        //implement stop here
        return OTC_TRUE;
    },

    .get_capture_settings = [](const otc_video_capturer *capturer, void *user_data, struct otc_video_capturer_settings *settings) -> otc_bool{
        struct custom_capturer *video_capturer = static_cast<struct custom_capturer *>(user_data);
        if (video_capturer == nullptr)
        {
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
};