//============================================================================
// 05_Speedometer - Animated speedometer
//
// Copyright (c) 2022, Jarkko Lempiainen
// All rights reserved.
//----------------------------------------------------------------------------
// This example shows how you can render flicker free 2D GUI using the
// rasterizer.
//
// From this example you will learn how to...
// - Define custom vertex input format
// - Use psc_geometry_cluster to render small bits of procedural geometry
// - Use vec2f pos type and generate uv-coordinates in vertex shader
// - Write and use tile shaders
// - Render 1-bit alpha textures
//============================================================================

#include "egl_device_lib.h"
#include "egl_shaders.h"
EGL_USING_NAMESPACE
#ifndef SAMPLE_DEVICE_DEFINED
typedef graphics_device_ili9341 gfx_device_t;
#endif
//----------------------------------------------------------------------------


//============================================================================
// Defines
//============================================================================
#define ROTATE_90_DEGREES 0 // Rotate rendering of the gauge by 90 degrees
//----------------------------------------------------------------------------


//============================================================================
// Textures and samplers
//============================================================================
// Gauge background texture and sampler
static const EGL_ALIGN(4) uint32_t PROGMEM s_ptx_gauge_bg[]=
{
  #include "ptx_gauge_bg.h"
};
static texture s_tex_gauge_bg;
typedef sampler2d<pixfmt_r5g6b5, texfilter_linear> smp_gauge_bg_t;
smp_gauge_bg_t s_smp_gauge_bg;
//----

// Gauge needle texture with 1-bit alpha and sampler
static const EGL_ALIGN(4) uint32_t PROGMEM s_ptx_gauge_needle[]=
{
  #include "ptx_gauge_needle.h"
};
static texture s_tex_gauge_needle;
sampler2d<pixfmt_r5g5b5a1, texfilter_point> s_smp_gauge_needle;
//----------------------------------------------------------------------------


//============================================================================
// rasterizer_memory_cfg
//============================================================================
struct rasterizer_memory_cfg: rasterizer_memory_cfg_base
{
  // Here we define the tile to have 16bit color format with 1-bit alpha. The
  // alpha is used to determine where we have rendered pixels for the needle
  // for the frame. This information is then used in the tile shader either
  // render the needle texture or render the background texture.
  enum {rt0_fmt=pixfmt_r5g5b5a1};
};
//----------------------------------------------------------------------------


//============================================================================
// setup
//============================================================================
// The below defines custom input vertex format for the needle. For the input
// vertex we only need 2D vertex position on the screen and we don't define
// texture coordinates for the needle texture but generate them in the vertex
// shader instead.
struct vertex_needle
{
  struct transform_state {template<class PSC> EGL_INLINE transform_state(const PSC&) {}};
  vec2f pos;
};
//----

// For rendering the needle we calculate the position of the vertices
// procedurally in code based on the required speed of the gauge. For this we
// allocate an array of input vertices for the needle and fixed list of triangle
// indices. Note that we allocate 8 vertices and 4 triangle indices even though
// the needle has only 4 vertices and 2 triangles (quad). The other 4 vertices
// hold the previous frame needle vertices, and by rendering the previous frame
// triangles (but discarding the pixels for those triangles in the pixel shader)
// tricks rasterizer to update those regions as well and wipe out the previous
// needle pixels as the result.
static vertex_needle s_needle_vertices[8];
static const uint8_t s_needle_indices[12]={0, 1, 2, 1, 3, 2, 4, 5, 6, 5, 7, 6};
//----

static gfx_device_t s_gfx_device;
static rasterizer_memory<gfx_device_t, rasterizer_memory_cfg> s_gfx_device_mem;
//----

void setup()
{
  // Init serial and graphics device.
  init_serial();
#ifndef SAMPLE_DEVICE_DEFINED
  s_gfx_device.init(10, 9, 13, 11, 12, 8);
#endif
  s_gfx_device_mem.init(s_gfx_device);
  s_gfx_device.clear_depth(cleardepth_max); /*todo: should be able to remove since we don't enable depth testing*/

  // Init the assets
  s_tex_gauge_bg.init(s_ptx_gauge_bg);
  s_tex_gauge_needle.init(s_ptx_gauge_needle);
  mem_zero(s_needle_vertices, sizeof(s_needle_vertices));

  // Draw the gauge background to setup the initial state for the gauge
#if ROTATE_90_DEGREES==0
  s_gfx_device.fast_draw_rect(0, 0, gfx_device_t::fb_width, gfx_device_t::fb_height,
                              ips_texture<smp_gauge_bg_t>(s_tex_gauge_bg, 0, 0, 1.0f/gfx_device_t::fb_width, 1.0f/gfx_device_t::fb_height));
#else
  s_gfx_device.fast_draw_rect(0, 0, gfx_device_t::fb_width, gfx_device_t::fb_height,
                              ips_texture<smp_gauge_bg_t, true>(s_tex_gauge_bg, 0, 0, -1.0f/gfx_device_t::fb_width, 1.0f/gfx_device_t::fb_height));
#endif
}
//----------------------------------------------------------------------------


//============================================================================
// vs_gauge_needle
//============================================================================
// This is gauge specific shader to render the gauge needle. We don't use the
// set_attributes() pattern here because this vertex shader is supposed to be
// always linked with the gauge pixel shader.
struct vs_gauge_needle
{
  template<class PSIn, class PSOState, class VIn>
  EGL_INLINE void exec(PSIn &psin_, const PSOState&, const typename VIn::transform_state&, const VIn &vin_, uint8_t vertex_idx_) const
  {
    // For position we simply write the procedurally calculated normalized
    // device coordinates (NDCs) to the output. For the texture coordinates
    // used for for needle texture we procedurally generate the coordinates
    // instead of reading them from the input vertex. Because we have specific
    // ordering of vertices for the quad (top-left, top-right, bottom-left,
    // bottom-right) we can calculate the UV's from the vertex index. We could
    // have also defined the vertex_needle to contain UV's and set those up
    // but this is a bit simpler and shows an example how to use vertex index.
    // The same shader is executed also for the previous frame vertices and
    // the UV's for those will not be correct, but since we don't render those
    // pixels (discard in the pixel shader) it doesn't really matter.
    psin_.pos=vin_.pos;
    psin_.uv.set(float(vertex_idx_&1), float(vertex_idx_>>1));
  }
};
//----------------------------------------------------------------------------


//============================================================================
// ps_gauge_needle
//============================================================================
// Here we define gauge needle specific pixel shader.
struct ps_gauge_needle
{
  // For pixel shader we need only the vertex positions as 2D coordinates and
  // UV's to fetch the needle texture. Note that previously we have used
  // vec4f for position to perform perspective correct 3D triangle rendering,
  // but for 2D rendering we are fine with 2D coordinates. The rasterizer
  // supports 2D, 3D and 4D vertex positions defined as vec2f, vec3f and vec4f
  // respectively and optimizes the rasterization for those cases. We could
  // also use vec4f and fill the z=0 and w=1, but that would be wasted
  // performance and memory.
  struct psin
  {
    vec2f pos;
    vec2f uv;
  };
  //--------------------------------------------------------------------------

  template<class PSC>
  EGL_INLINE void exec(rasterizer_pixel_out<PSC> &psout_, const typename PSC::pso_state&, const psin &v0_, const psin &v1_, const psin &v2_, const vec3f &bc_, uint8_t prim_idx_) const
  {
    // Here we use primitive (triangle) index to discard pixels for the two
    // previous frame triangles (index=2 and index=3). For the current frame
    // triangles we fetch the texture using the UV's calculated in the vertex
    // shader. Next we perform alpha-testing to discard pixels which have
    // alpha=0 set for the texture.
    if(prim_idx_<2)
    {
      vec2f uv=v0_.uv*bc_.x+v1_.uv*bc_.y+v2_.uv*bc_.z;
      pixel<pixfmt_r5g5b5a1> smp;
      s_smp_gauge_needle.sample(smp, s_tex_gauge_needle, uv);
      if(smp.c.a)
        psout_.export_rt0(smp);
    }
  }
};
//----------------------------------------------------------------------------


//============================================================================
// rstate_gauge_needle
//============================================================================
struct rstate_gauge_needle: rasterizer_psc_base
{
  enum {rt0_fmt=rasterizer_memory_cfg::rt0_fmt};
  enum {tbfc_mode=rstate_tbfc_none};
};
//----------------------------------------------------------------------------


//============================================================================
// tshader_gauge
//============================================================================
// Here we define a new shader type called "tile shader". A tile shader can be
// executed at the commit() to perform custom pixel processing for the the
// rasterized pixels before being transformed to the display frame buffer.
// The output pixel format of the shader is also the frame buffer format and
// not the tile format, i.e. the shader also performs the format conversion.
// In this case we first read a pixel from the tile render target. If the pixel
// has alpha=1 (i.e. it was written out by the needle pixel shader) we write
// out the pixel result, otherwise we write background image at the pixel
// position. This results the current position of the needle to be rendered
// but the previous frame needle position to be cleared out.
struct tshader_gauge
{
  template<e_pixel_format dst_fmt>
  EGL_INLINE void exec(pixel<dst_fmt> &res_, const void *const*rts_, const rasterizer_depth_target&, size_t src_px_offs_, uint16_t x_, uint16_t y_) const
  {
    // Read the tile pixel and check if the alpha=1, i.e. if the pixel is part
    // of the needle.
    typedef pixel<e_pixel_format(rasterizer_memory_cfg::rt0_fmt)> rt0_fmt_t;
    const rt0_fmt_t smp=((const rt0_fmt_t*)rts_[0])[src_px_offs_];
    if(smp.c.a)
    {
      // Write out the needle pixel to output
      res_=smp;
      return;
    }

    // Sample the background at the pixel position
#if ROTATE_90_DEGREES==0
    s_smp_gauge_bg.sample(res_, s_tex_gauge_bg, vec2f((x_+0.5f)/gfx_device_t::fb_width, (y_+0.5f)/gfx_device_t::fb_height));
#else
    s_smp_gauge_bg.sample(res_, s_tex_gauge_bg, vec2f((y_+0.5f)/gfx_device_t::fb_height, -(x_+0.5f)/gfx_device_t::fb_width));
#endif
  }
};
//----------------------------------------------------------------------------


//============================================================================
// update_gauge_needle
//============================================================================
// Update the gauge needle position. The normalized speed is value in [0, 1]
// range from 0 to the max speed.
void update_gauge_needle(float normalized_speed_)
{
  // Define the needle properties to match the graphics. For different gauge
  // background where the needle center can be in different location, or
  // different needle length or max speed these values needs to be tweaked.
  static const float s_rcp_aspect_ratio=float(s_tex_gauge_bg.height())/float(s_tex_gauge_bg.width());
  static const vec2f s_needle_center_pos(0.0f, -0.3f); // NDC coordinates for needle center (needle rotates about this position)
  static const float s_needle_base_offset=-0.233f; // Offset from the needle center where the base of the needle is
  static const float s_angle_speed_0=3.6f; // The angle (in radians) for zero speed
  static const float s_angle_speed_max=-0.46f; // The angle (in radians) for max speed

  // Calculate the needle vectors for needle base, tip and right-vector. These
  // vectors are then used to define the 4 needle vertex positions
  const float needle_half_width=s_tex_gauge_needle.width()/float(s_tex_gauge_bg.width());
  const float needle_length=2.0f*s_tex_gauge_needle.height()/float(s_tex_gauge_bg.height());
  float angle=lerp(s_angle_speed_0, s_angle_speed_max, normalized_speed_);
  float cos_angle=cos(angle), sin_angle=sin(angle);
  vec2f needle_base(s_needle_center_pos.x+cos_angle*s_needle_base_offset, s_needle_center_pos.y+sin_angle*s_needle_base_offset);
  vec2f needle_tip(needle_base.x+cos_angle*needle_length, needle_base.y+sin_angle*needle_length);
  vec2f needle_right(sin_angle*needle_half_width, -cos_angle*needle_half_width);

  // Setup the needle vertex positions (0-3=new positions, 4-7=previous frame positions)
  mem_copy(s_needle_vertices+4, s_needle_vertices, sizeof(s_needle_vertices)/2);
#if ROTATE_90_DEGREES==0
  s_needle_vertices[0].pos.set((needle_tip.x-needle_right.x)*s_rcp_aspect_ratio,  needle_tip.y-needle_right.y);
  s_needle_vertices[1].pos.set((needle_tip.x+needle_right.x)*s_rcp_aspect_ratio,  needle_tip.y+needle_right.y);
  s_needle_vertices[2].pos.set((needle_base.x-needle_right.x)*s_rcp_aspect_ratio, needle_base.y-needle_right.y);
  s_needle_vertices[3].pos.set((needle_base.x+needle_right.x)*s_rcp_aspect_ratio, needle_base.y+needle_right.y);
#else
  s_needle_vertices[0].pos.set(needle_tip.y-needle_right.y, -(needle_tip.x-needle_right.x)*s_rcp_aspect_ratio);
  s_needle_vertices[1].pos.set(needle_tip.y+needle_right.y, -(needle_tip.x+needle_right.x)*s_rcp_aspect_ratio);
  s_needle_vertices[2].pos.set(needle_base.y-needle_right.y, -(needle_base.x-needle_right.x)*s_rcp_aspect_ratio);
  s_needle_vertices[3].pos.set(needle_base.y+needle_right.y, -(needle_base.x+needle_right.x)*s_rcp_aspect_ratio);
#endif

  // Draw the needle using the geometry cluster PSC. We just setup the calculated
  // vertices and constant vertex indices to the PSC and dispatch.
  psc_geometry_cluster<rstate_gauge_needle, vertex_needle, vs_gauge_needle, ps_gauge_needle> needle_pso;
  needle_pso.set_geometry(s_needle_vertices, 8, s_needle_indices, 4);
  s_gfx_device.dispatch_pso(needle_pso);

  // Now we instantiate the tile shader and commit using the shader, which
  // performs the rendering of the needle to the current position and clearing
  // the old position pixels with the gauge background texture.
  tshader_gauge tsh;
  s_gfx_device.commit(tsh);
}
//----------------------------------------------------------------------------


//============================================================================
// loop
//============================================================================
#ifndef ARDUINO
float millis() {return 0;} // On Arduino millis() returns milliseconds since the program start
#endif
//----------------------------------------------------------------------------

void loop()
{
  // Animate the speed
  float t=0.3f*millis()/1000.0f+9.7f;
  float speed=clamp(0.2f+0.15f*sin(t)+0.075f*sin(t*2.27f), 0.0f, 1.0f);

  // Update the needle
  update_gauge_needle(speed);
  psc_geometry_cluster<rstate_gauge_needle, vertex_needle, vs_gauge_needle, ps_gauge_needle> needle_pso;
  needle_pso.set_geometry(s_needle_vertices, 8, s_needle_indices, 4);
  s_gfx_device.dispatch_pso(needle_pso);
  tshader_gauge tsh;
  s_gfx_device.commit(tsh);
}
//----------------------------------------------------------------------------
