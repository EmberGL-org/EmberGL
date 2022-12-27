//============================================================================
// 04_TexCube - Texture Cube
//
// Copyright (c) 2022, Jarkko Lempiainen
// All rights reserved.
//----------------------------------------------------------------------------
// This example shows how to render 3D objects with textures. We have covered
// most of this in previous samples (loading and sampling textures in 2D
// rectangle rendering, and interpolating vertex attributes with barycentric
// coordinates), but it's worth to reiterate since texture sampling in shaders
// is quite an important topic.
//
// From this example you will learn how to...
// - Read 2D texture coordinates in vertex shader
// - Interpolate the coordinates in pixel shader and sample the texture
// - Select barycentric coordinate interpolation mode.
//============================================================================

#include "egl_device_lib.h"
#include "egl_mesh.h"
#include "egl_vertex.h"
#include "egl_texture.h"
EGL_USING_NAMESPACE
#ifndef SAMPLE_DEVICE_DEFINED
typedef graphics_device_ili9341 gfx_device_t;
#endif
//----------------------------------------------------------------------------


//============================================================================
// 3D geometry
//============================================================================
// This cube geometry contains UV-coordinates and was generated using vertex
// format p48n32uv32 with the Meshlete tool
static const EGL_ALIGN(4) uint32_t PROGMEM s_p3g_cube_data[]=
{
  #include "p3g_cube.h"
};
static p3g_mesh s_p3g_cube;
typedef vertex_p48n32uv32 vtx_cube_t;
//----------------------------------------------------------------------------


//============================================================================
// Texture and samplers
//============================================================================
static const EGL_ALIGN(4) uint32_t PROGMEM s_ptx_smiley_data[]=
{
  #include "ptx_smiley.h"
};
static texture s_tex_smiley;
typedef sampler2d<pixfmt_r5g6b5, texfilter_linear, texaddr_wrap> smp_r5g6b5_linear_wrap_t;
//----------------------------------------------------------------------------


//============================================================================
// rasterizer_memory_cfg
//============================================================================
struct rasterizer_memory_cfg: rasterizer_memory_cfg_base
{
  enum {rt0_fmt=gfx_device_t::fb_format};
};
//----------------------------------------------------------------------------


//============================================================================
// setup
//============================================================================
static gfx_device_t s_gfx_device;
static rasterizer_memory<gfx_device_t, rasterizer_memory_cfg> s_gfx_device_mem;
static cameraf s_camera;
//----

void setup()
{
  // Init serial and graphics device
  init_serial();
#ifndef SAMPLE_DEVICE_DEFINED
  s_gfx_device.init(10, 9, 13, 11, 12, 8);
#endif
  s_gfx_device_mem.init(s_gfx_device);
  s_gfx_device.clear_depth(cleardepth_max);

  // Init the assets
  s_p3g_cube.init(s_p3g_cube_data);
  s_tex_smiley.init(s_ptx_smiley_data);

  // Setup camera projection and the transform.
  float cam_fov=60.0f;
  float cam_aspect_ratio=float(gfx_device_t::fb_width)/float(gfx_device_t::fb_height);
  float cam_near_plane=0.1f;
  float cam_far_plane=100.0f;
  mat44f view2proj=perspective_matrix<float>(cam_fov*mathf::deg_to_rad, cam_aspect_ratio, cam_near_plane, cam_far_plane);
  s_camera.set_view_to_proj(view2proj, cam_near_plane, cam_far_plane);
  s_camera.set_view_to_world(tform3f::identity());
}
//----------------------------------------------------------------------------


//============================================================================
// vs_uv
//============================================================================
// Simple vertex shader that transform vertex position and passes texture
// coordinates from input vertex to vertex out.
struct vs_uv
{
  //==========================================================================
  // vsout
  //==========================================================================
  struct vsout
  {
    vec2f uv;
  };
  //--------------------------------------------------------------------------

  template<class PSIn, class PSOState, class VIn>
  EGL_INLINE void exec(PSIn &psin_, const PSOState &pso_state_, const typename VIn::transform_state &tstate_, const VIn &vin_, uint8_t) const
  {
    // Transform vertex position and calculate n.l
    psin_.pos=vec4f(get_pos(vin_, tstate_), 1.0f)*pso_state_.obj_to_proj;
    vsout vo;
    vo.uv=get_uv(vin_, tstate_);
    psin_.set_attribs(vo);
  }
};
//----------------------------------------------------------------------------


//============================================================================
// ps_tex
//============================================================================
// Simple pixel shader which interpolates the texture coordinates with the
// barycentric coordinates and samples a single texture with it.
template<class Sampler>
struct ps_tex
{
  //==========================================================================
  // psin
  //==========================================================================
  struct psin
  {
    vec4f pos;
    vec2f uv;
    //------------------------------------------------------------------------

    template<class VSOut> EGL_INLINE void set_attribs(const VSOut &vo_)
    {
      uv=vo_.uv;
    }
  };
  //--------------------------------------------------------------------------

  template<class PSC>
  EGL_INLINE void exec(rasterizer_pixel_out<PSC> &psout_, const typename PSC::pso_state&, const psin &v0_, const psin &v1_, const psin &v2_, const vec3f &bc_, uint8_t) const
  {
    // Interpolate UV-coordinates with the barycentric coordinates.
    vec2f uv=v0_.uv*bc_.x+v1_.uv*bc_.y+v2_.uv*bc_.z;

    // Sample the texture with the interpolated coordinatess
    color_rgbaf res;
    sampler.sample(res, *tex, uv);
    psout_.export_rt0(res);
  }
  //--------------------------------------------------------------------------

  const texture *tex;
  Sampler sampler;
};
//----------------------------------------------------------------------------


//============================================================================
// rstate_opaque
//============================================================================
struct rstate_opaque: rasterizer_psc_base
{
  // Define depth buffer format
  enum {rt0_fmt=rasterizer_memory_cfg::rt0_fmt};
  enum {depth_format=rasterizer_memory_cfg::depth_format};

  // Here we select how the barycentric coordinates are interpolated. Normally
  // for quality you want perspective correct interpolation, but in some cases
  // might opt to non-perspective interpolation for better performance. The
  // perspective correct interpolation requires a division per pixel, but the
  // cost is fixed regardless of how many vertex attributes are interpolated.
  // The cases where you might consider disabling perspective correction:
  // - For 2D rendering (e.g. GUI) there is no error using non-perspective
  //   interpolation.
  // - If triangles are small on the screen (e.g. highly tessellated mesh or
  //   far from camera), the error from non-perspective correct interpolation
  //   is negligible.
  // - If the result from interpolated attributes changes little over the
  //   triangle, you might consider disabling the perspective correction (e.g.
  //   plain n.l vertex lighting in the 02_RotoMonkey example)
  // Optimizing the interpolation is better left for later stage in project
  // development (except for the 2D case) to be able to profile what's the
  // actual performance impact of the interpolation mode with the final
  // shaders and if it's worth the potential quality drop.
  // In this example the error is more noticeable because large cube triangles,
  // so it's a good test case to see the impact of noperspective, try it!
  enum {bci_mode=rstate_bcimode_perspective};
};
//----------------------------------------------------------------------------


//============================================================================
// loop
//============================================================================
void loop()
{
  // Setup cube transform.
  static float s_rot_x=-0.8f, s_rot_y=-0.7f;
  vec3f obj_pos(0.0f, 0.0f, 4.0f);
  mat33f obj_rot;
  set_rotation_xyz(obj_rot, s_rot_x, s_rot_y, 0.0f);
  s_rot_x+=0.0123f;
  s_rot_y+=0.0321f;

  // setup PSO and commit
  psc_p3g_mesh<rstate_opaque, vtx_cube_t, vs_uv, ps_tex<smp_r5g6b5_linear_wrap_t> > pso;
  pso.set_geometry(s_p3g_cube, 0);
  pso.set_transform(s_camera, tform3f(obj_rot, obj_pos));
  pso.pshader().tex=&s_tex_smiley;
  s_gfx_device.dispatch_pso(pso);

  // Commit the dispatches to the device.
  s_gfx_device.commit();
}
//----------------------------------------------------------------------------
