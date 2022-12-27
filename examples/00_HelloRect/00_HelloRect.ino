//============================================================================
// 00_HelloRect - Simple rectangle rendering example
// 
// Copyright (c) 2022, Jarkko Lempiainen
// All rights reserved.
//----------------------------------------------------------------------------
// This example demonstrates the basic graphics device initialization and
// immediate mode rectangle rendering using a simple immediate mode pixel
// shader.
//
// Immediate mode interface draw-functions send the pixels "immediately" and
// synchronously to the display when the function is called. They don't either
// require setup of the rasterizer so it's a bit easier to get started using
// the functions. Immediate mode shaders have a bit different interface from
// rasterizer shaders, but the idea is the same, so this is a simple first
// look into how shaders work.
//
// From this example you will learn how to...
// - select proper display driver for your project
// - initialize the display
// - write a custom immediate mode pixel shader
// - draw rectangle with the custom shader
//============================================================================

// The line below includes the header containing all graphics drivers included
// in the library. Ideally you would just #include the header of the driver
// you are going to use, but this is just for simplicity.
#include "egl_device_lib.h"

// Everything in EmberGL headers is wrapped into "egl" namespace to avoid
// potential naming clashes with other libraries. The line below bring
// everything in the egl namespace to global namespace by effectively doing
// "using namespace egl;", so that the library classes and functions can be
// used without "egl::" prefix. However, if in some unfortunate case you get
// a naming clash, you can omit this line and use "egl::" prefix instead.
// Many classes and functions use "egl_" prefix to avoid clashes even when
// using global namespace, but there are some classes for example without
// the prefix and more generic name with higher clash potential (e.g. "vec3f")
EGL_USING_NAMESPACE

// The #ifndef below is just to be able to use this exact sample code outside
// of Arduino environment on PC. In your own code you don't need to wrap the
// device type selection and initialization with this macro #ifdef if you plan
// to run your code on Arduino only. Basically on PC we can define and
// initialize the graphics device outside of this file and #define the macro
// to skip the definition and initialization here. Because the rest of the
// code is portable there the rest of the code works as is.
#ifndef SAMPLE_DEVICE_DEFINED
// Here we select the graphics device matching the display. The available
// drivers can be found in "src/drivers" directory.
typedef graphics_device_ili9341 gfx_device_t;
#endif
//----------------------------------------------------------------------------


//============================================================================
// setup
//============================================================================
static gfx_device_t s_gfx_device;
//----

void setup()
{
  // As the first thing we initialize the serial monitor so that in the case
  // we have errors we see the error message.
  init_serial();

#ifndef SAMPLE_DEVICE_DEFINED
  // Initialize the graphics device to match the way you have connected the
  // the pins between your MCU and display. Below we assume that MCU pins
  // are connected to ILI9341 pins as follows:
  // CS=10, DC=9, SCLK=13, MOSI=11, MISO=12 (reset pin is connected to 3.3V)
  // If you use a different display driver, the init() function takes different
  // parameters and needs to be changed to match the HW configuration.
  s_gfx_device.init(10, 9, 13, 11, 12, 8);
#endif
}
//----------------------------------------------------------------------------


//============================================================================
// test_shader
//============================================================================
struct test_shader
{
  // For this custom test shader we construct the shader taking the rectangle
  // coordinates and a frame number as arguments. This could be something
  // completely different depending what you want to do.
  test_shader(int16_t x_, int16_t y_, uint8_t frame_) :x(x_), y(y_), frame(frame_) {}
  //--------------------------------------------------------------------------

  // Pixel shader function executed for every rectangle pixel. This function
  // is called with the dst_fmt template argument matching the display native
  // pixel format. Different devices may use different native pixel format
  // (e.g. r5g6b5 or r8b8g8a8) but function doesn't need to be changed because
  // by calling res_.set_rgba8() the passed 8-bit RGBA values are converted to
  // the appropriate pixel format. We also use EGL_INLINE to ensure that this
  // function gets inlined and doesn't have function call overhead.
  template<e_pixel_format dst_fmt> EGL_INLINE void exec(pixel<dst_fmt> &res_, uint16_t x_, uint16_t y_) const
  {
    // Calculate pixel RGB color for pixel at [x_, y_] screen coordinates.
    // we just subtract the rectangle coordinates from the screen coordinates
    // to have RG color gradients relatively to the rectangle. for B we just
    // use frame count to animate the color.
    // this is very simple example of a custom shader, but you could basically
    // do any kind of calculation here, read images, etc. to calculate the
    // final pixel color. Just play around!
    res_.set_rgba8((uint8_t)(x_-x)*2, (uint8_t)(y_-y)*2, frame, 0);
  }
  //--------------------------------------------------------------------------

  int16_t x, y;
  uint8_t frame;
};
//----------------------------------------------------------------------------


//============================================================================
// loop
//============================================================================
void loop()
{
  // Draw rectangle of width rect_width and height rect_height in the middle
  // of the display using the immediate mode draw_rect() function and the
  // shader we defined above. For the shader we pass the rectangle coordinates
  // and frame count as we defined it above.
  // Note that here we use draw_rect() but there's also fast_draw_rect(). The
  // difference is that draw_rect() clips the rectangle coordinates to the
  // screen bounds, while the fast-version doesn't, so it's a bit faster
  // (particularly if you draw a lot of small rectangles). If you know that
  // the rectangle is completely within the display (like in our case, unless if
  // you render to a tiny display), you could use the fast_draw_rect(). It's
  // "undefined behavior" to draw outside the screen bounds with a fast-function
  // (e.g. could assert, or have strange visual results depending on display).
  static uint8_t s_frame=0;
  enum {rect_width=128, rect_height=128};
  int16_t x=gfx_device_t::fb_width/2-rect_width/2;
  int16_t y=gfx_device_t::fb_height/2-rect_height/2;
  s_gfx_device.draw_rect(x, y, rect_width, rect_height, test_shader(x, y, s_frame));
  ++s_frame;
}
//----------------------------------------------------------------------------
