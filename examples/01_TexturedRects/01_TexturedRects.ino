//============================================================================
// 01_TexturedRects - Simple textured rectangles
//
// Copyright (c) 2022, Jarkko Lempiainen
// All rights reserved.
//----------------------------------------------------------------------------
// This example demonstrates how you can use textures in your programs. Here
// we still use the immediate mode draw_rect() for the sake of simplicity.
// Sampling the texture within rasterizer shaders is the same but the texture
// coordinates are just calculated a bit differently.
//
// From this example you will learn how to...
// - Convert JPG/PNG/etc. images to PTX format and import to your program
// - Initialize the texture resource
// - Define a sampler for sampling (reading) textures
// - Sample the texture and perform operations with the texture sample
//============================================================================

#include "egl_device_lib.h"
#include "egl_texture.h"
EGL_USING_NAMESPACE
#ifndef SAMPLE_DEVICE_DEFINED
typedef graphics_device_ili9341 gfx_device_t;
#endif
//----------------------------------------------------------------------------


//============================================================================
// Texture and samplers
//============================================================================
// Here we define the texture asset in array s_ptx_smiley_data (on Arduino
// "PROGMEM" allocates the array in flash memory), where the texture data
// resides in file "ptx_smiley.h" file that's generated with "ptx_conv" tool.
// The tool converts JPG/PNG/etc. image files to PTX format in the required
// pixel format. This specific file is generated with the following command line:
//   ptx_conv -i smiley.png -o ptx_smiley.h -hexd -f r5g6b5 
// The used smiley.png can be found in the 01_TextureRects example dir. For the
// explanation of the tool arguments and options you can run "ptx_conv -h".
// Because the array uses type uint32_t the "-hexd" must be used to generate
// an array of 32-bit types, while with "-hex" the data is 8-bit instead and
// the array type must be changed to uint8_t. Using 32-bit types is faster to
// compile but some platforms may not support this. The type has no impact on
// run-time performance or memory usage. Note that on PC/Mac/etc. you probably
// want to dynamically allocate memory instead and read the texture from disk
// as a binary blob to the allocated memory.
//
// In ptx_smiley.h file comments you can find the parameters of the texture,
// such as texture size and pixel format. For sampling (reading) the texture
// we need to define a "sampler" that matches the texture type and format.
// The sampler also defines the texture addressing mode, i.e. what happens
// when you sample the texture outside of coordinates [0, 1]. Below we define
// two sampler types of type sampler2d (because we sample 2d texture) and
// pixfmt_r5g6b5 because this is the format mentioned in ptx_smiley.h file.
// One sampler uses "wrap" mode, i.e. the texture coordinates repeat outside
// of [0, 1] range, and the other uses "clamp" mode, which clamps the
// coordinates to [0, 1] range.
static const EGL_ALIGN(4) uint32_t PROGMEM s_ptx_smiley_data[]=
{
  #include "ptx_smiley.h"
};
static texture s_tex_smiley;
typedef sampler2d<pixfmt_r5g6b5, texfilter_linear, texaddr_wrap> smp_r5g6b5_linear_wrap_t;
typedef sampler2d<pixfmt_r5g6b5, texfilter_linear, texaddr_clamp> smp_r5g6b5_linear_clamp_t;
//----------------------------------------------------------------------------


//============================================================================
// setup
//============================================================================
static gfx_device_t s_gfx_device;
//----

void setup()
{
  // Init serial and graphics device
  init_serial();
#ifndef SAMPLE_DEVICE_DEFINED
  s_gfx_device.init(10, 9, 13, 11, 12, 8);
#endif

  // Initialize the texture with the texture data. It's good to initialize
  // textures after init_serial() because in case of errors we can see the
  // error messages in the serial monitor. After the initialization the
  // texture is ready for sampling.
  s_tex_smiley.init(s_ptx_smiley_data);
}
//----------------------------------------------------------------------------


//============================================================================
// test_shader
//============================================================================
// Here we define a shader which samples the texture. This shader takes the
// sampler "Sampler" as a template argument, so we can use the same code with
// different samplers.
template<class Sampler>
struct test_shader
{
  // construction
  test_shader(const texture *tex_, int16_t x_, int16_t y_, float scale_x_, float scale_y_, const color_rgbaf &tint_)
    :tex(tex_), x(x_), y(y_), scale_x(scale_x_), scale_y(scale_y_), tint(tint_) {}
  //--------------------------------------------------------------------------

  // pixel shading
  template<e_pixel_format dst_fmt> EGL_INLINE void exec(pixel<dst_fmt> &res_, uint16_t x_, uint16_t y_) const
  {
    // Finally, lets sample the texture. First we calculate the texture
    // coordinates relatively to the coordinates we passed to the shader
    // constructor and scale it with the passed scaling values. Remember
    // that texture coordinates vary between [0, 1] across the texture.
    float u=(x_-x)*scale_x;
    float v=(y_-y)*scale_y;
    // Next we bilinear sample the texture to color_rgbaf because we want to
    // perform some math operations with the texture sample and doing so with
    // color_rgbaf is quite convenient. If we would like to just sample the
    // texture and without performing any operations with the sample, We could
    // also sample the result to directly to the res_ (or any pixel<> type)
    // as follows and it would be faster because there would be no float
    // conversion:
    //   sampler.sample(res_, *texture, uv);
    color_rgbaf col;
    sampler.sample(col, *tex, vec2f(u, v));
    // Now we multiply the color with the tint color and write to the result
    // to res_
    res_=col*tint;
  }
  //--------------------------------------------------------------------------

  const texture *tex;
  int16_t x, y;
  float scale_x, scale_y;
  color_rgbaf tint;
  Sampler sampler;
};
//----------------------------------------------------------------------------


//============================================================================
// loop
//============================================================================
void loop()
{
  // Draw 50x50px quad to the upper left corner. The original texture is 64x64px
  // so this downscales it a bit.
  {
    int16_t x=0, y=0, width=50, height=50;
    test_shader<smp_r5g6b5_linear_clamp_t> sh(&s_tex_smiley, x, y, 1.0f/width, 1.0f/height, color_rgbaf(1.0f, 1.0f, 1.0f, 1.0f));
    s_gfx_device.draw_rect(x, y, width, height, sh);
  }

  // Let's now draw 100x100 rectangle next to the previous rectangle. Because the
  // rectangle is larger, it upscales the texture a bit instead.
  {
    int16_t x=50, y=0, width=100, height=100;
    test_shader<smp_r5g6b5_linear_clamp_t> sh(&s_tex_smiley, x, y, 1.0f/width, 1.0f/height, color_rgbaf(1.0f, 1.0f, 1.0f, 1.0f));
    s_gfx_device.draw_rect(x, y, width, height, sh);
  }

  // Now let's have a bit fun and draw a 64x64px rectangle, but flip it vertically
  // and tint the texture with reddish color.
  {
    int16_t x=150, y=0, width=64, height=64;
    test_shader<smp_r5g6b5_linear_clamp_t> sh(&s_tex_smiley, x, y+64, 1.0f/width, -1.0f/height, color_rgbaf(1.0f, 0.5f, 0.5f, 0.5f));
    s_gfx_device.draw_rect(x, y, width, height, sh);
  }

  // Let's try the wrap address mode now! Let's draw 64x64 quad but tile the texture
  // twice horizontally and vertically.
  {
    int16_t x=0, y=100, width=64, height=64;
    test_shader<smp_r5g6b5_linear_wrap_t> sh(&s_tex_smiley, x, y, 2.0f/width, 2.0f/height, color_rgbaf(1.0f, 1.0f, 1.0f, 1.0f));
    s_gfx_device.draw_rect(x, y, width, height, sh);
  }

  // We don't have to scale the rectangle uniformly but can squeeze it too!
  {
    int16_t x=64, y=100, width=100, height=50;
    test_shader<smp_r5g6b5_linear_clamp_t> sh(&s_tex_smiley, x, y, 1.0f/width, 1.0f/height, color_rgbaf(1.0f, 1.0f, 1.0f, 1.0f));
    s_gfx_device.draw_rect(x, y, width, height, sh);
  }
}
//----------------------------------------------------------------------------
