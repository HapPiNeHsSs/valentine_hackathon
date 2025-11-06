//Linux Screen Capture via X1 (headers)
//Beejay Urzo
//Vonage CSE 8/8/2024

#include "screen_capture.h"

ScreenShot::ScreenShot(int x, int y, int width, int height):
    x(x),
    y(y),
    width(width),
    height(height)
{
    display = XOpenDisplay(nullptr);
    root = DefaultRootWindow(display);
}

ScreenShot::ScreenShot()
{
    x = 0;
    y = 0;
    Screen *screen;    
    display = XOpenDisplay(nullptr);
    screen = ScreenOfDisplay(display, 0);
    width = screen->width;
    height = screen->height;
    root = DefaultRootWindow(display);
}

//Use Open CV to get Image, more flexible, but more overhead
void ScreenShot::get_screenshot_opencv(cv::Mat& cvImg)
{
    if(img != nullptr)
        XDestroyImage(img);
    img = XGetImage(display, root, x, y, width, height, AllPlanes, ZPixmap);
    cvImg = cv::Mat(height, width, CV_8UC4, img->data);
}

//Use Raw Pixel, potentially Faster
XImage* ScreenShot::get_screenshot_pixel()
{
    if(img != nullptr)
        XDestroyImage(img);
    return XGetImage(display, root, x, y, width, height, AllPlanes, ZPixmap);
}

ScreenShot::~ScreenShot()
{
    if(img != nullptr)
        XDestroyImage(img);
    XCloseDisplay(display);
}
