//============================================================================
// 07_ClusterCulling - Cluster culling example
//
// Copyright (c) 2022, Jarkko Lempiainen
// All rights reserved.
//----------------------------------------------------------------------------
// This example shows how the rasterizer culls geometry clusters, to give better
// idea about geometry processing and how it can be optimized.
//
// From this example you will learn how to...
// - Optimize opaque object rendering
// - How visibility cones and occlusion culling eliminates clusters
// - Use programmable blending pipeline for additive blending
//============================================================================

#include "egl_device_lib.h"
#include "egl_mesh.h"
#include "egl_vertex.h"
#include "egl_shaders.h"
EGL_USING_NAMESPACE
#ifndef SAMPLE_DEVICE_DEFINED
typedef graphics_device_ili9341 gfx_device_t;
#endif
//----------------------------------------------------------------------------


//============================================================================
// 3D geometry
//============================================================================
static const EGL_ALIGN(4) uint32_t PROGMEM s_p3g_cup_data[]=
{
  #include "p3g_cup.h"
};
static p3g_mesh s_p3g_cup;
typedef vertex_p48n32 vtx_cup_t;
//----

static const EGL_ALIGN(4) uint32_t PROGMEM s_p3g_dragon_data[]=
{
  #include "p3g_dragon.h"
};
static p3g_mesh s_p3g_dragon;
typedef vertex_p48n32 vtx_dragon_t;

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
  s_p3g_cup.init(s_p3g_cup_data);
  s_p3g_dragon.init(s_p3g_dragon_data);

  // Setup camera projection.
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
// rstate_cluster_overdraw
//============================================================================
struct rstate_cluster_overdraw: rasterizer_psc_base
{
  // Here we set a special render state to visualize cluster culling (both
  // v-cone and occlusion culling). We want to have depth test & writes enabled
  // so that Hi-Z is properly updated for occlusion culling. However, for the
  // cluster culling visualization purposes we want to render pixels even if
  // they fail depth testing. For this purpose we enable a special debug render
  // state "debug_disable_pixel_depth_test". We also disable triangle back-face
  // culling to be able to see all triangles and how v-cone culling eliminates
  // some of the clusters (e.g. inside the cup). All the triangles are rendered
  // with additive blending to see the overdraw.
  enum {depth_format=rasterizer_memory_cfg::depth_format};
  enum {rt0_fmt=rasterizer_memory_cfg::rt0_fmt};
  enum {tbfc_mode=rstate_tbfc_none};
  enum {debug_disable_pixel_depth_test=true};
  EGL_RT_BLENDFUNC(0, blendfunc_add);
};
//----------------------------------------------------------------------------


//============================================================================
// rstate_opaque
//============================================================================
struct rstate_opaque: rasterizer_psc_base
{
  enum {depth_format=rasterizer_memory_cfg::depth_format};
  enum {rt0_fmt=rasterizer_memory_cfg::rt0_fmt};
};
//----------------------------------------------------------------------------


//============================================================================
// ps_simple_lighting
//============================================================================
struct ps_simple_lighting
{
  //==========================================================================
  // psin
  //==========================================================================
  struct psin
  {
    vec4f pos;
    vec3f normal;
    //------------------------------------------------------------------------

    template<class VSOut> EGL_INLINE void set_attribs(const VSOut &vo_) {normal=vo_.normal;}
  };
  //--------------------------------------------------------------------------

  template<class PSC> EGL_INLINE void exec(rasterizer_pixel_out<PSC> &psout_, const typename PSC::pso_state&, const psin &v0_, const psin &v1_, const psin &v2_, const vec3f &bc_, uint8_t) const
  {
    // Perform simple double sided n.l lighting with fixed light direction
    vec3f normal=v0_.normal*bc_.x+v1_.normal*bc_.y+v2_.normal*bc_.z;
    psout_.export_rt0(abs(dot(normal, unit(vec3f(1.0f, 1.0f, -1.0f)))));
  }
};
//----------------------------------------------------------------------------


//============================================================================
// loop
//============================================================================
#ifndef ARDUINO
float millis() {return 8000.0f;} // On Arduino millis() returns milliseconds since the program start
#endif
//----------------------------------------------------------------------------

void loop()
{
  // Setup time
  float time=millis()/1000.0f;

  // Calculate cup and dragon transforms
  mat33f cup_rot;
  set_rotation_xyz(cup_rot, -mathf::pi*0.5f, -time*0.5f, 0.0f);
  vec3f cup_pos=vec3f(0.0f, -7.0f, 18.0f);
  mat33f dragon_rot;
  set_rotation_xyz(dragon_rot, time*0.123f, time*0.321f, 0.0f);
  vec3f dragon_pos=vec3f(0.0f, sin(time)*4.0f+2.0f, 18.0f);

  // Switch to debug visualization after 5 seconds of simple lighting rendering
  bool use_debug_rendering=time>5.0f;
  if(use_debug_rendering)
  {
    // Render the cup with debug visualization (use greenish tint)
    psc_p3g_mesh<rstate_cluster_overdraw, vtx_cup_t, vs_static, ps_color<pixfmt_r32g32b32a32f> > pso;
    pso.pshader().color=color_rgbaf(0.1f, 0.3f, 0.1f);
    pso.set_geometry(s_p3g_cup, 0);
    pso.set_transform(s_camera, tform3f(cup_rot, cup_pos));
    s_gfx_device.dispatch_pso(pso);

    // Render the dunkin' dragon (use reddish tint). Note that it's important
    // to render the cup before the dragon in order for occlusion culling to be
    // able to eliminate dragon clusters (try swapping them around). Here we
    // know that the cup occludes dragon so we can hardcode the rendering order,
    // but in general it's a good strategy render objects in increasing distance
    // from camera by sorting the objects before rendering (e.g. with qsort()).
    pso.pshader().color=color_rgbaf(0.3f, 0.1f, 0.1f);
    pso.set_geometry(s_p3g_dragon, 0);
    pso.set_transform(s_camera, tform3f(dragon_rot, dragon_pos));
    s_gfx_device.dispatch_pso(pso);
  }
  else
  {
    // Render the cup with simple n.l lighting
    psc_p3g_mesh<rstate_opaque, vtx_cup_t, vs_static, ps_simple_lighting> pso;
    pso.set_geometry(s_p3g_cup, 0);
    pso.set_transform(s_camera, tform3f(cup_rot, cup_pos));
    s_gfx_device.dispatch_pso(pso);

    // Render dragon with simple n.l lighting
    pso.set_geometry(s_p3g_dragon, 0);
    pso.set_transform(s_camera, tform3f(dragon_rot, dragon_pos));
    s_gfx_device.dispatch_pso(pso);
  }

  // Commit the dispatches to the device.
  s_gfx_device.commit();
}
//----------------------------------------------------------------------------
