//============================================================================
// 02_RotoMonkey - Rotating monkey head example
//
// Copyright (c) 2022, Jarkko Lempiainen
// All rights reserved.
//----------------------------------------------------------------------------
// This example shows the basics of rendering 3D objects with EmberGL. There
// can be quite a bit of concepts to digest, particularly if you are not already
// familiar with 3D rendering and other graphics APIs. However, just getting the
// example running and starting to play around with different parameters and
// modify things is a good way to get a good grasp on how it all work.
//
// From this example you will learn how to...
// - Configure the rasterizer for the graphics device
// - Convert OBJ/FBX/etc. 3D objects to P3G and import to your program
// - Initialize the 3D mesh and select proper run-time vertex format
// - Write simple vertex and pixel shaders
// - Basic 3D camera and object transformation setup
// - Setup PSO for rendering and dispatch to graphics device
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
// Here we define a 3D geometry P3G asset in an array s_p3g_monkey_data, where
// the mesh data resides in "p3g_monkey.h" file, that's generated with
// "Meshlete" tool. The tool converts OBJ/FBX/DAE/etc. mesh files to P3G format
// using required vertex format and other parameters. Note that P3G files
// contain only 3D geometry data and no materials/textures, which needs to be
// handled separately.
// This specific file was generated with the following command line:
//   meshlete -i monkey.dae -o p3g_monkey.h -hexd -vf p48n32 -mb -mc
// The used "monkey.dae" (Collada) file can be found in the 02_RotoMonkey
// example dir. There are couple of other models as well "p3g_cube.h" and
// "p3g_torus.h" you can try out, and the default monkey 3D model could be too
// heavy for some MCUs.
//
// The tool splits 3D geometry to geometry "clusters", where each cluster
// contains some small number of triangles and vertices. By default the tool
// uses <=64 vertices and <=128 triangles per cluster but these defaults can
// be overridden with "-mv" and "-mt" switches respectively. The clustered
// geometry processing enables various optimizations in the rasterizer which
// is why this process is done.
// 
// The "-vf" switch specifies the vertex format used for the geometry. For all
// P3G files in this example we use "p48n32" format, which means 48-bit position
// and 32-bit normal data, which adds up to 10 bytes per vertex. The used vertex
// format for .h file can be found in the comment of the file. Depending on the
// use-case you may want to use different format, for example to add texture
// coordinates, colors or tangent space. The available default vertex formats
// can be found in "vfmt.xml" file of Meshlete tool, but you can add your own
// formats as well (either to this file, or to separate file that you can tell
// the tool to use with "-vc" switch). The used vertex format in P3G must match
// the format used by the vertex shader that reads the data. For the default
// vertex formats you can find the accompanying C++ vertex types in egl_vertex.h
// file, so for this choise we define the used vertex type as vertex_p48n32 type.
//
// The "-mb" switch tells the tool to calculate bounding volumes for each
// cluster. This switch should be always used, because the rasterizer uses the
// volumes for tile binning and occlusion testing, which are improtant for
// performance. You can see in "Options" comment in the generated .h file if
// this switch was used.
//
// The "-mc" switch tell the tool to generate "visibility cones" for the
// clusters, which are used for run-time view (camera) dependent culling of the
// geometry to further optimize performance. Visibility cones define conical
// regions in space for each cluster where the cluster is visible and if the
// view isn't in this region, the cluster is culled early in the rendering
// pipeline improving the performance. For simple geometry it's not worth to use
// -mc (it's not used for p3g_cube or p3g_torus) because all clusters are
// visible all around the object, but this can save quite a bit of processing
// for more complex geometry. The memory and performance overhead of the
// visibility cones is quite small though, so it's better to use the switch in
// doubt.
static const EGL_ALIGN(4) uint32_t PROGMEM s_p3g_monkey_data[]=
{
  #include "p3g_monkey.h"
};
static p3g_mesh s_p3g_monkey;
typedef vertex_p48n32 vtx_monkey_t;
//----------------------------------------------------------------------------


//============================================================================
// rasterizer_memory_cfg
//============================================================================
// This config struct defines the global rasterizer settings for buffers that
// are needed by the rasterizer. The rasterizer doesn't allocate any memory
// dynamically but all the buffers are allocated from static/global memory with
// this struct.
//
// The struct should be derived from rasterizer_memory_cfg_base, which defines
// the default rasterizer settings, but they can be individually overridden in
// the struct. While the default settings would work for this example, we orride
// them all to explain their meaning in more details. It can be quite overwhelming
// to understand all the settings at this point because it requires deeper
// understanding of the rasterizer, so it's probably better to just skim them
// through and return back to them later.
//
// Allocating too small buffers results in run-time error with an error message
// for debug and release builds, while for retail it just crashes because buffer
// overflows, so it's good to use debug/release while developing. To optimize
// the buffers and RAM usage it's good to first allocate larger buffers and then
// use egl_device::::log_rasterizer_stats() to print out how much of the buffers
// were actually used, and then trim down the buffers using the provided numbers.
struct rasterizer_memory_cfg: rasterizer_memory_cfg_base
{
  // The value below defines the used depth buffer format. For this example we
  // use 16bpp depth buffer, which is enough depth precision in this case. You
  // can also use 8bpp format or 32bpp format depending on the depth precision
  // requirements.
  enum {depth_format=depthfmt_uint16};

  // Hi-Z is a low-resolution representation of the depth buffer to enable fast
  // region depth testing. This can be used for example for conservative cluster
  // occlusion culling to skip processing clusters if they are completely
  // occluded by geometry rasterized earlier in the frame. Because of this it's
  // good to rasterize opaque geometry in the order of increasing distance from
  // camera to increase the occlusion opportunities. There is a cost associated
  // in both updating and testing against Hi-Z (and some minor memory overhead),
  // but usually it's good to have Hi-Z enabled.
  enum {depth_hiz=true};

  // Here we specify the intermediate tile pixel format to be the same as the
  // native display pixel format. Everything is first rendered to the tile
  // buffer of this format that resides in RAM and after the tile has been
  // completed, the pixels are converted and transferred to the native display
  // frame buffer. Why would you use anything but the native format you ask?
  // You could do frame buffer blending (e.g. alpha or additive) which might
  // require higher precision than the native format, or want to perform some
  // operations with the rendered result (e.g. deferred lighting) using tile
  // shaders once the tile rendering is complete. Also for plain grayscale
  // rendering 8bpp pixel format could be used to really squeeze the RAM usage
  // and expand the final result to the native format upon pixel transfer.
  // You can also use up to 4 multiple render targets (MRTs) by defining the
  // format rtile*_fmt for each. By default the other render tile formats are
  // set to none and thus don't allocate memory.
  enum {rt0_fmt=gfx_device_t::fb_format};

  // The value below defines how many PSO dispatches can be done between in a
  // frame (between commit() calls). In this example we render only a single
  // object with single material per frame so this is all we need. For better
  // explanation about PSO's check out README.md. This value must be at least
  // "Max dispatches" reported by the rasterizer stats.
  enum {max_dispatches=1};

  // This defines how much memory is allocated for all the dispatched PSO's
  // between commits. The size depends how big PSO's are used to render the
  // frame. This value must be at least "Max PSO store" reported by the
  // rasterizer stats.
  enum {pso_store_size=128};

  // This value defines the maximum memory required by a PSO state. This buffer
  // is allocated temporarily from the stack and setting this value to the
  // smallest required value reduces the stack usage. This value must be at
  // least "Max PSO state" reported by the rasterizer stats.
  enum {max_pso_state_size=128};

  // The frame is rendered in tiles and here We can define the tile size.
  // Larger tiles require larger memory buffer for intermediate results, while
  // for smaller tiles a single cluster needs to be binned to more tiles and
  // thus use more memory for the binning. From the performance point of view
  // larger tiles require processing more clusters per tile, while with smaller
  // tiles there are more tiles to process in total. It's really a balancing
  // act which tile size is the best fit, but this is a good starting point.
  enum {tile_width=64, tile_height=64};

  // This defines the order in which the tiles are rendered. Below we specify
  // Morton order which walks through the tiles in more local manner. The main
  // benefit is that the post-transform vertex cache is likely better utilized
  // because the same cluster is likely processed for consecutively rendered
  // tiles, thus reducing the vertex transform cost.
  enum {tile_order=tileorder_morton};

  // This is the maximum number of total clusters binned to tiles between
  // commits. Note that if a cluster is culled before binning (e.g. with
  // v-cones, or because it's out of screen) it doesn't count towards the
  // total cluster count. This value must be at least "Max clusters" reported
  // by the rasterizer stats.
  enum {max_clusters=256};

  // For binning clusters to tiles we build a linked list of cluster index
  // strips for each tile that holds indices to the clusters binned to the
  // tile. Each strip can contain some number of cluster indices. Depending
  // how the clusters are binned we need different amount of strips and for
  // the same object this varies depending how the object is rendered.
  // It's not clear yet what's the best way to adjust this value but it's
  // better to be quite conservative of the reported value. This value must
  // be at least "Max cluster strips" reported by the rasterizer stats.
  enum {max_cluster_strips=256};

  // This is the size of a temporal post-transform vertex buffer (PTV buffer)
  // to hold the vertex transform result of a single cluster for the PSC
  // tform_cluster() function. The required size depends on how many vertices
  // are in the clusters and what's the size of the PTVs (ptv_fmt) of the used
  // PSCs. Because for P3G generation we use <=64 vertices in a cluster and the
  // sizeof(ps_simple_ndotl::psin) is 20, we use value 64*20. This value must
  // be at least "Max PTV buffer size" reported by the rasterizer stats.
  enum {max_vout_size=64*20};

  // This is the size of the PTV cache (or v-cache for short). The cache is
  // used to reduce cluster vertex transform cost across tiles, i.e. if the
  // same cluster is binned to multiple tiles, the transform result could be
  // stored in the cache and reused when the other tiles are rendered. Here
  // we just disable the v-cache by setting the size to 0. There is no minimum
  // value but a larger v-cache reduces vertex transforms.
  enum {vcache_size=0};

  // The below value defines how many tiles worth of memory we want to allocate
  // for DMA transfers. The DMA buffers are allocated in native pixel format.
  // If tiles are rasterized faster than DMA has time to transfer them to the
  // display they are queued to the buffers for transfer. Note that some tiles
  // may be faster to rasterize than others so using more buffers can help to
  // utilize wait times of faster tiles to rasterize slower tiles. Note that if
  // the device doesn't support DMA no buffers are allocated regardless of this
  // value.
  enum {max_dma_transfers=0};
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

  // After the initialization we need to clear the content of the depth buffer.
  // It depends on the case to which value you should clear the depth. Here we
  // clear the depth to max depth value because we use regular depth buffer
  // where smaller values mean close to the camera. However, for reverse depth
  // buffer rendering you should clear the depth to min instead.
  // Once the depth is cleared it doesn't need to be cleared again for every
  // frame, because the rasterizer keeps automatically clearing it to the set
  // value after each tile.
  s_gfx_device.clear_depth(cleardepth_max);

  // Initialize the P3G mesh with the geometry data defined earlier. It's
  // good to initialize the mesh after init_serial() because in case of errors
  // we can see the error messages in the serial monitor. After initialization
  // the geometry is ready for rendering.
  s_p3g_monkey.init(s_p3g_monkey_data);

  // Setup camera projection with camera FoV, aspect ratio and near & far planes.
  // This is usually one-time setup after which only the camera position and
  // orientation are animated.
  float cam_fov=60.0f; // the FOV in degrees
  float cam_aspect_ratio=float(gfx_device_t::fb_width)/float(gfx_device_t::fb_height);
  float cam_near_plane=0.1f;
  float cam_far_plane=100.0f;
  mat44f view2proj=perspective_matrix<float>(cam_fov*mathf::deg_to_rad, cam_aspect_ratio, cam_near_plane, cam_far_plane);
  s_camera.set_view_to_proj(view2proj, cam_near_plane, cam_far_plane);

  // Because we don't move the camera in this example, we can setup the camera
  // transform in the setup() as well. The below code places the camera to
  // [0, 0, 0] and sets the rotation to identity matrix (X=right, Y=up, Z=forward)
  vec3f cam_pos(0.0f, 0.0f, 0.0f);
  mat33f cam_rot(1.0f, 0.0f, 0.0f,
                 0.0f, 1.0f, 0.0f,
                 0.0f, 0.0f, 1.0f);
  s_camera.set_view_to_world(tform3f(cam_rot, cam_pos));

  // Lets setup the light direction (unit length in world space)
  s_light_dir=unit(vec3f(1.0f, 1.0f, -1.0f));
}
//----------------------------------------------------------------------------


//============================================================================
// vs_simple_ndotl
//============================================================================
// This defines a vertex shader used for rendering the geometry. This vertex
// shader implements the interface expected by psc_p3g_mesh and in theory you
// could have any kind of shader interface for custom PSCs. The rasterizer
// dictates only the PSC interface as defined by rasterizer_psc_base but how
// those requirements are "forwarded" is up to the PSC.
// That said, it's good to standardize the shader interface across PSCs so that
// shaders can be more freely shared across different PSCs.
struct vs_simple_ndotl
{
  //==========================================================================
  // vsout
  //==========================================================================
  // This struct defines the vertex attributes outputted by the vertex shader,
  // except for vertex position. 
  struct vsout
  {
    float ndotl;
  };
  //--------------------------------------------------------------------------

  template<class PSIn, class PSOState, class VIn>
  EGL_INLINE void exec(PSIn &psin_, const PSOState &pso_state_, const typename VIn::transform_state &tstate_, const VIn &vin_, uint8_t) const
  {
    // Get position attribute from the input vertex. All the vertex attributes
    // should be fetched with get_*() functions so that the data can be
    // transparently transformed. For example the position data in input vertex
    // could be stored as vec3f, or it could be stored as quantized vec3s in
    // object bounds and get_pos() would dequantize the position. From the
    // shader code point of view there is no difference thus the shader is
    // compatible with any vertex input which provides the data, regardless of
    // the format.
    vec3f pos=get_pos(vin_, tstate_);

    // Transform the object space position from object space to projection/clip
    // space. Note that the matrix convention is row-major and we multiply from
    // left-to-right order. The rasterizer expects the pos value to be in
    // projection/clip space like with other graphics APIs.
    psin_.pos=vec4f(pos, 1.0f)*pso_state_.obj_to_proj;

    // Let's also fetch normal from the input vertex. Just like for position,
    // we use a get-function to fetch the data to allow transform of the data
    // if necessary.
    vec3f normal=get_normal(vin_, tstate_);

    // For vertex output we fill the vsout struct defined above. We could write
    // the attributes directly psin_ like the position, but there's a reason
    // for this indirection: Vertex shader may output more data than expected
    // by the pixel shader, which would result in compilation error. By using
    // this indirection we can avoid the error and the unnecessary data and
    // related calculations will be optimized out by the C++ compiler (due to
    // having no side-effects). This way we expand the combinatory power of the
    // shaders without sacrificing performance.
    // The indirection doesn't make a difference in this example because the
    // shaders are limited to the scope of this example, but for building a
    // shader library it's very useful.
    // Here we output single attribute "ndotl", which is the dot product
    // between the object space vertex normal and light direction in object
    // space.
    vsout vo;
    vo.ndotl=dot(normal, light_obj_dir);
    psin_.set_attribs(vo);
  }
  //--------------------------------------------------------------------------

  vec3f light_obj_dir;
};
//----------------------------------------------------------------------------


//============================================================================
// ps_simple_ndotl
//============================================================================
struct ps_simple_ndotl
{
  //==========================================================================
  // psin
  //==========================================================================
  // This struct defines the actual post-transform vertex type. The only
  // mandatory attribute is "vec4f pos" that's expected by the rasterizer.
  // Vertex shader must write clip/projection space vertex coordinates to pos,
  // which the  rasterizer transforms to normalized device coordinates (NDC).
  // The rest of the attributes can be freely defined.
  // Here we expect the associated vertex shader to provide argument "ndotl".
  // Violating the requirement results in compilation error. If the vertex
  // shader provides other attributes, they are omitted by the set_attribs()
  // function and eliminated by the compiler.
  struct psin
  {
    vec4f pos;
    float ndotl;
    //------------------------------------------------------------------------

    template<class VSOut>
    EGL_INLINE void set_attribs(const VSOut &vo_)
    {
      ndotl=vo_.ndotl;
    }
  };
  //--------------------------------------------------------------------------

  template<class PSC>
  EGL_INLINE void exec(rasterizer_pixel_out<PSC> &psout_, const typename PSC::pso_state&, const psin &v0_, const psin &v1_, const psin &v2_, const vec3f &bc_, uint8_t) const
  {
    // The following line performs vertex attribute linear interpolation on the
    // rasterized triangle using the passed barycentric coordinates. This same
    // interpolation code can be used for any vertex attribute (colors, texture
    // coordinates, tangent space, etc.)
    float ndotl=v0_.ndotl*bc_.x+v1_.ndotl*bc_.y+v2_.ndotl*bc_.z;

    // Export the result to the render target 0. We use abs() here to have
    // double-sided lighting and multiply the result with object color.
    psout_.export_rt0(color*abs(ndotl));
  }
  //--------------------------------------------------------------------------

  color_rgbaf color;
};
//----------------------------------------------------------------------------


//============================================================================
// rstate_opaque
//============================================================================
// This struct defines the render state for opaque rendering we can use in
// this example. The default render state is fine for this, except we need
// to override the rt0_fmt and depth_format to match the format we chose in
// the rasterizer_memory_cfg.
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
  // Here we setup the object transformation. The position of the object is
  // [0, 0, 3], i.e. in the front of the camera that we set up in the setup()
  // function. For the rotation we use two angles s_rot_x and s_rot_y that we
  // animate, and setup 3x3 rotation matrix with the angles. Note that the
  // angles are in radians not degrees.
  static float s_rot_x=-1.2f, s_rot_y=-0.5f;
  vec3f obj_pos(0.0f, 0.0f, 3.0f);
  mat33f obj_rot;
  set_rotation_xyz(obj_rot, s_rot_x, s_rot_y, 0.0f);
  s_rot_x+=0.0123f;
  s_rot_y+=0.0321f;

  // Here we create a PSO by instantiating psc_p3g_mesh class for vtx_monkey_t
  // input vertex format, and the above vertex and pixel shaders. It also uses
  // the previously defined rstate_opaque render state struct to setup the
  // rendering pipeline for opaque rendering for this PSO.
  psc_p3g_mesh<rstate_opaque, vtx_monkey_t, vs_simple_ndotl, ps_simple_ndotl> pso;

  // Lets now setup the object transform to the PSO. psc_p3g_mesh provides
  // set_transform() function to setup all the necessary matrices needed to
  // render the mesh. Besides transforming the vertices in the vertex shader
  // with the matrix, psc_p3g_mesh must also handle cluster culling and binning
  // to tiles. Otherwise we could just have the obj->proj matrix in the vertex
  // shader.
  pso.set_transform(s_camera, tform3f(obj_rot, obj_pos));

  // To setup the vertex shader we need to setup the light direction in object
  // space. To properly rotate a vector with a 3x3 matrix we need to multiply
  // the vector by inverse-transpose of the rotation matrix. We have obj_rot
  // which transform from object->world space, but we need world->object space
  // instead to transform the light direction in world space to the object
  // space, which means we need to invert obj_rot matrix. However, this means
  // we need to invert the matrix twice which cancels out, so we only need to
  // transpose the matrix instead. However, multiplying the vector from the
  // left instead of right does the trick without the transpose.
  pso.vshader().light_obj_dir=unit(obj_rot*s_light_dir);

  // Let's set a light cyan color of the object
  pso.pshader().color.set(0.5f, 1.0f, 1.0f);

  // Here we setup the P3G geometry to render. The second parameter defines
  // the geometry "segment" index, which can be used to render objects with
  // multiple materials. The P3G files in this example have only single
  // segment so we just use index 0, and dispatch only one PSO for the mesh.
  // For multi-segment objects we could simply loop over the segments
  // for(uint16_t seg_idx=0; seg_idx<s_p3g_monkey.num_segments(); ++seg_idx)
  pso.set_geometry(s_p3g_monkey, 0);

  // And finally we can dispatch the PSO for rasterization. The dispatch
  // doesn't yet actually rasterize the geometry, but only bins the clusters
  // to the tiles for rasterization. The rasterization is deferred until call
  // to commit(), which does rasterize all the tiles and submit the rasterized
  // tiles to the display frame buffer.
  // So for the frame we first call dispatch_pso() for all the objects we want
  // to render and then at the end of the frame call commit()
  s_gfx_device.dispatch_pso(pso);

  // And there you go, we commit the dispatched PSOs for rasterization. It's
  // also possible to pass "tile shader" to the commit, which can be used to
  // to perform custom tile=>frame buffer pixel transforms.
  s_gfx_device.commit();

  // Here we print out the rasterizer statistics to the log every 100 frames,
  // which can be used to trim the rasterizer memory configuration values.
  // Remember to enable either EGL_DEBUG or EGL_RELEASE build in egl_core.h to
  // have the statistics gathered. The statistics are disabled in EGL_RETAIL
  // to optimize performance.
  static unsigned s_log_counter=0;
  if(s_log_counter==0)
  {
    s_gfx_device.log_rasterizer_stats();
    s_log_counter=100;
  }
}
//----------------------------------------------------------------------------
