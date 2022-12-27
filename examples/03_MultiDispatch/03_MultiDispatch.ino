//============================================================================
// 03_MultiDispatch - Rendering multi-segment objects and many objects
//
// Copyright (c) 2022, Jarkko Lempiainen
// All rights reserved.
//----------------------------------------------------------------------------
// This example shows how to render objects with multiple materials and how
// vertex and pixel shaders can be combined.
//
// From this example you will learn how to...
// - Render objects with multiple materials/segments
// - Render multiple objects in the scene
// - Efficiently combine vertex and pixel shaders
//============================================================================

#include "egl_device_lib.h"
#include "egl_mesh.h"
#include "egl_vertex.h"
EGL_USING_NAMESPACE
#ifndef SAMPLE_DEVICE_DEFINED
typedef graphics_device_ili9341 gfx_device_t;
#endif
//----------------------------------------------------------------------------


//============================================================================
// 3D geometry
//============================================================================
// This monkey geometry has two segments, where the eyes and head are separated
// to different segments. You can open 03_MultiDispatch/monkey_2mat.dae in
// Blender to see the two segments.
static const EGL_ALIGN(4) uint32_t PROGMEM s_p3g_monkey_2mat_data[]=
{
  #include "p3g_monkey_2mat.h"
};
static p3g_mesh s_p3g_monkey_2mat;
typedef vertex_p48n32 vtx_monkey_t;
//----

// This is the torus geometry. Unlike in the previous example, this is generated
// with vertex format "pn"
static const EGL_ALIGN(4) uint32_t PROGMEM s_p3g_torus_data[]=
{
  #include "p3g_torus.h"
};
static p3g_mesh s_p3g_torus;
typedef vertex_pn vtx_torus_t;
//----------------------------------------------------------------------------


//============================================================================
// rasterizer_memory_cfg
//============================================================================
// For the rasterizer memory config use default settings, but just override the
// RT0 pixel format to be native device pixel format.
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
static vec3f s_light_dir;
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
  s_p3g_monkey_2mat.init(s_p3g_monkey_2mat_data);
  s_p3g_torus.init(s_p3g_torus_data);

  // Setup camera projection and the transform (use identity for transform,
  // which means that the position=[0, 0, 0] and x-axis=right, y-axis=up and
  // z-axis=forward).
  float cam_fov=60.0f;
  float cam_aspect_ratio=float(gfx_device_t::fb_width)/float(gfx_device_t::fb_height);
  float cam_near_plane=0.1f;
  float cam_far_plane=100.0f;
  mat44f view2proj=perspective_matrix<float>(cam_fov*mathf::deg_to_rad, cam_aspect_ratio, cam_near_plane, cam_far_plane);
  s_camera.set_view_to_proj(view2proj, cam_near_plane, cam_far_plane);
  s_camera.set_view_to_world(tform3f::identity());

  // Setup the light direction
  s_light_dir=unit(vec3f(1.0f, 1.0f, -1.0f));
}
//----------------------------------------------------------------------------


//============================================================================
// vs_ndotl
//============================================================================
// Simple vertex shader that transforms vertex position and calculates n.l
struct vs_ndotl
{
  //==========================================================================
  // vsout
  //==========================================================================
  struct vsout
  {
    float ndotl;
  };
  //--------------------------------------------------------------------------

  template<class PSIn, class PSOState, class VIn>
  EGL_INLINE void exec(PSIn &psin_, const PSOState &pso_state_, const typename VIn::transform_state &tstate_, const VIn &vin_, uint8_t) const
  {
    // Transform vertex position and calculate n.l
    psin_.pos=vec4f(get_pos(vin_, tstate_), 1.0f)*pso_state_.obj_to_proj;
    vsout vo;
    vo.ndotl=dot(get_normal(vin_, tstate_), light_obj_dir);
    psin_.set_attribs(vo);
  }
  //--------------------------------------------------------------------------

  vec3f light_obj_dir;
};
//----------------------------------------------------------------------------


//============================================================================
// ps_ndotl
//============================================================================
// Simple pixel shader which interpolates ndotl and multiplies the abs() of the
// result with given color.
struct ps_ndotl
{
  //==========================================================================
  // psin
  //==========================================================================
  struct psin
  {
    vec4f pos;
    float ndotl;
    //------------------------------------------------------------------------

    template<class VSOut> EGL_INLINE void set_attribs(const VSOut &vo_)
    {
      ndotl=vo_.ndotl;
    }
  };
  //--------------------------------------------------------------------------

  template<class PSC>
  EGL_INLINE void exec(rasterizer_pixel_out<PSC> &psout_, const typename PSC::pso_state&, const psin &v0_, const psin &v1_, const psin &v2_, const vec3f &bc_, uint8_t) const
  {
    float ndotl=v0_.ndotl*bc_.x+v1_.ndotl*bc_.y+v2_.ndotl*bc_.z;
    psout_.export_rt0(color*abs(ndotl));
  }
  //--------------------------------------------------------------------------

  color_rgbaf color;
};
//----------------------------------------------------------------------------


//============================================================================
// ps_color
//============================================================================
// Simple pixel shader which outputs a constant color.
struct ps_color
{
  //==========================================================================
  // psin
  //==========================================================================
  struct psin
  {
    vec4f pos;
    //------------------------------------------------------------------------

    template<class VSOut> EGL_INLINE void set_attribs(const VSOut&) {}
  };
  //--------------------------------------------------------------------------

  template<class PSC>
  EGL_INLINE void exec(rasterizer_pixel_out<PSC> &psout_, const typename PSC::pso_state&, const psin&, const psin&, const psin&, const vec3f&, uint8_t) const
  {
    // output the color set for the pixel shader
    psout_.export_rt0(color);
  }
  //--------------------------------------------------------------------------

  color_rgbaf color;
};
//----------------------------------------------------------------------------


//============================================================================
// rstate_opaque
//============================================================================
struct rstate_opaque: rasterizer_psc_base
{
  enum {rt0_fmt=rasterizer_memory_cfg::rt0_fmt};
  enum {depth_format=rasterizer_memory_cfg::depth_format};
};
//----------------------------------------------------------------------------


//============================================================================
// loop
//============================================================================
void loop()
{
  // Setup monkey transform.
  vec3f monkey_pos(0.0f, 0.0f, 4.0f);
  mat33f monkey_rot(1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, -1.0f,
                    0.0f, 1.0f, 0.0f);

  // Setup PSO for the monkey head (without eyes) and dispatch. The monkey head
  // has segment index 0, that we pass to set_geometry(). We'll use the same
  // ndotl lighting as in the previous example and color the head green.
  psc_p3g_mesh<rstate_opaque, vtx_monkey_t, vs_ndotl, ps_ndotl> pso_monkey_0;
  pso_monkey_0.set_geometry(s_p3g_monkey_2mat, 0);
  pso_monkey_0.set_transform(s_camera, tform3f(monkey_rot, monkey_pos));
  pso_monkey_0.vshader().light_obj_dir=unit(monkey_rot*s_light_dir);
  pso_monkey_0.pshader().color.set(0.0f, 1.0f, 0.0f);
  s_gfx_device.dispatch_pso(pso_monkey_0);

  // Setup PSO for the monkey eyes and dispatch. The eyes have segment index 1,
  // that's passed to set_geometry(). We'll use the constant color pixel shader
  // that we defined above for the eyes instead. However, we don't have to write
  // another vertex shader just for the eyes because vs_ndotl exports all
  // attributes that's required by the pixel shader. Because the ndotl argument
  // isn't used by the pixel shader, all the code in vertex shader used to
  // calculate the attribute is eliminated by the compiler. From performance
  // point of view this is the same as writing special vertex shader for eyes
  // without ndotl calculation.
  psc_p3g_mesh<rstate_opaque, vtx_monkey_t, vs_ndotl, ps_color> pso_monkey_1;
  pso_monkey_1.set_geometry(s_p3g_monkey_2mat, 1);
  pso_monkey_1.set_transform(s_camera, tform3f(monkey_rot, monkey_pos));
  pso_monkey_1.pshader().color.set(1.0f, 0.0f, 0.0f);
  s_gfx_device.dispatch_pso(pso_monkey_1);

  // Let's also render a blue torus that rotates around the monkey. For this
  // we'll use the same vertex and pixel shaders as for the monkey head.
  // However, torus uses different vertex format (pn) than the monkey head
  // (p48n32). Because the input vertex data fetching is done with the get-
  // functions, there are no changes needed for the vertex shader code.
  psc_p3g_mesh<rstate_opaque, vtx_torus_t, vs_ndotl, ps_ndotl> pso_torus;
  static const float s_rad=2.0f; // the torus movement radius around the monkey head
  static const float s_torus_scale=0.5f; // the size scale of the torus
  static float s_t=0.2f;
  s_t+=0.02f;
  mat33f torus_rot;
  set_rotation_xyz(torus_rot, s_t*2.2f, s_t*3.7f, 0.0f);
  pso_torus.set_geometry(s_p3g_torus, 0);
  pso_torus.set_transform(s_camera, tform3f(torus_rot*s_torus_scale, monkey_pos+vec3f(cos(s_t)*s_rad, 0.0f, sin(s_t)*s_rad)));
  pso_torus.vshader().light_obj_dir=unit(torus_rot*s_light_dir);
  pso_torus.pshader().color.set(0.0f, 0.0f, 1.0f);
  s_gfx_device.dispatch_pso(pso_torus);

  // Commit the dispatches to the device.
  s_gfx_device.commit();
}
//----------------------------------------------------------------------------
