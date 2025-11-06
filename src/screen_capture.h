//Linux Screen Capture via X1 (implementation)
//Beejay Urzo
//Vonage CSE 8/8/2024


#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/core.hpp>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

class ScreenShot
{
    private:
        Display* display;
        Window root;
        XImage* img{nullptr};
    public:
        int x,y,width,height;
    
    
        ScreenShot(int x, int y, int width, int height);
           

        ScreenShot();
       

        //Use Open CV to get Image, more flexible, but more overhead
        void get_screenshot_opencv(cv::Mat& cvImg);
       

        //Use Raw Pixel, potentially Faster
        XImage* get_screenshot_pixel();
       
        ~ScreenShot();
        
};