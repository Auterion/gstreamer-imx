gstreamer-imx
=============

About
-----

This is a set of [GStreamer 1.0](http://gstreamer.freedesktop.org/) plugins for NXP's
i.MX platform, which make use of the i.MX multimedia capabilities.

Currently tested on: i.MX6DL, i.MX6Q, i.MX7, i.MX8m quad/mini/plus, i.MX8 QuadMax/QuadXPlus


License
-------

These plugins are licensed under the LGPL v2.


Available decoder and encoder elements
--------------------------------------

There is one decoder element for each format the i.MX SoC supports. The list of supported formats depends on
the i.MX variant. For example, if h.264 is supported, there will be a `imxvpudec_h264` element.
The same is true of the encoder elements.

For the full list of formats that could theoretically be supported, check out the [libimxvpuapi library](https://github.com/Freescale/libimxvpuapi)
version 2.3.0 or later.


Elements for hardware accelerated 2D processing
-----------------------------------------------

Video transform, video sink, and compositor elements are available that perform their operations using
hardware blitters.

The following element types are available (not all are possible with all 2D blitter APIs):

* videotransform : A combination of scaling, 90-degree-step rotation, horizontal/vertical flipping,
  and color space conversion operations. These elements are able to render video overlay
  compositions from [GstVideoOverlayCompositionMeta data](https://gstreamer.freedesktop.org/documentation/video/gstvideooverlaycomposition.html#GstVideoOverlayCompositionMeta).
  They also implement the [GstVideoDirection interface](https://gstreamer.freedesktop.org/documentation/video/gstvideodirection.html)
  and have a "video-direction" property that handles rotation and flipping. Through this property,
  it is possible to configure these elements to auto-rotate images according to the information
  in [image-orientation tags](https://developer.gnome.org/gstreamer/stable/gstreamer-GstTagList.html#GST-TAG-IMAGE-ORIENTATION:CAPS).
* videosink : Render video frames to the Linux framebuffer. All operations that videotransform
  elements can handle can also be handled by these sinks. In addition, tearing-free playback is
  possible by making use of framebuffer page flipping that is tied to vsync. (Set the `use-vsync`
  property to TRUE to make use of this.) Aspect ratio can be preserved - excess space on the
  framebuffer is then letterboxed. Just like the videotransform elements, these video sinks can
  handle GstVideoOverlayCompositionMeta data. However, see the notes below.
* compositor : Assembles multiple input video streams into one output frame, just like the standard
  GStreamer compositor element. Its properties match those of GStreamer's standard compositor, making
  these 2D blitter compositor elements drop-in replacements for the standard compositor (which doe
  the compositing with the CPU). The pads in these blitter based compositors have additional properties
  for rotation, aspect ratio preservation, margins, and margin colors.

NOTE: Compositor elements are only available with GStreamer 1.16 or later. Compositor support
in GStreamer 1.14 was not yet in gst-plugins-base and had serious bugs.

NOTE: 2D blitter video sink elements do not work on i.MX8 machines, since these no longer use
the older MXC framebuffer driver (which these video sink elements rely on).

The following blitters are supported by these elements:

* G2D : 2D blitter driven by the Vivante GPU. Available on most i.MX6 and i.MX8 machines. G2D
        is emulated using the Display Processing Unit (DPU) on the i.MX8 QuadMax and QuadPlus.
        This is the most flexible of the available APIs, but might consume additional GPU power.
        There are videosink, videotransform, and compositor elements that use this API.
* PxP : Pixel Pipeline. Available on some i.MX6 and i.MX7 SoCs. 
        There are videosink and videotransform elements that use this API. Alpha blending
        with this API is currently tricky, which is why there is no compositor element (yet).
* IPU : Image Processing Unit. Available on some i.MX6 SoCs.
        Due to serious limitations of the driver, there is no compositor element available
        with this API.

All elements use internal "uploader" code that uploads frames into DMA memory if necessary. If
incoming frames are not aligned in a way that is compatible with what the blitters require, internal
frame copies are automatically done. These frame copies are CPU-based, so performance may suffer,
but otherwise, such frames could not be processed at all.


Notes about 2D video sinks and overlay composition
--------------------------------------------------

GstVideoOverlayCompositionMeta support in video sinks needs to be explicitly enabled by setting
the `render-overlays` property to TRUE. This is both for backwards compatibility and because of
flickering artifacts when vsync is not used. With vsync is turned off, that flickering appears,
because it is actually caused by tearing.

Consider the following example: A video frame is to be shown with a small overlay at the top left
corner. Without vsync, it is possible that the scanout driver scans and displays the contents of
the framebuffer after the main video frame was written to it but _before_ the overlay is painted.
Then, the next frame is written to the framebuffer, and this time, the scanout driver scans and
displays the contents just _after_ the overlay is finished. Due to this asynchronicity between
overlay painting and scanout driver, the overlay sometimes is shown, sometimes is not shown,
and flickering is the outcome.

Tearing is fixed by enabling vsync. So is this timing problem. VSync makes sure that the contents
that were written to the framebuffer are shown only after _all_ of the rendering is done.
Consequently, when a video sink shall do the overlay composition, it is _strongly_ recommended
to also turn on vsync.


Special Video4Linux2 elements for i.MX6
---------------------------------------

The `mxc_v4l2` V4L2 driver on the i.MX6 for various capture devices such as the OmniVision OV5640 is
unfortunately severely broken, and requires numerous workarounds in userspace. Furthermore, it contains
several i.MX specific extras for working with physical addresses. For that reason, GStreamer's `v4l2src`
and `v4l2sink` elements do not work on the i.MX6 for these devices.

To fix this, gstreamer-imx contains two elements that access V4L2 and apply workarounds for the
driver issues. These elements are:

* `imxv4l2videosrc`  : `v4l2src` equivalent for capturing video frames
* `imxv4l2videosink` : `v4l2sink` equivalent for displaying video frames

**NOTE:** These elements exist _only_ as a workaround for these driver bugs, since fixing that driver
is impractical. They are not meant to be used on any newer i.MX platform, since starting with the
i.MX8, a different (and better) driver is used.

The `-Dv4l2-mxc-source-sink` Meson option can be passed to meson to enable / disable these two elements.
If set to `true` (the default value), they will be enabled. `false` will disable them.


Special Video4Linux2 memory-to-memory elements for i.MX8 QuadMax/QuadXPlus
--------------------------------------------------------------------------

The imx-kernels support the Amphion VPU that is present on the i.MX8 QuadMax/QuadXPlus through the
V4L2 memory-to-memory API. However, imx-kernels 5.4 and older have a VPU driver that is riddled
with severe bugs that required substantial patching of GStreamer's V4L2 stack, causing the v4l2
elements to not behave in a stable manner. To be able to use the decoder, code was added to
the gstreamer-imx v4l2 plugin that enabkes Amphion decoder elements. Their names start with
`imxv4l2amphiondec_`. For example, an h264 decoder is called `imxv4l2amphiondec_h264`. With this,
it becomes possible to use the Amphion Malone decoder on the i.MX8 QuadMax/QuadXPlus even with
an unpatched GStreamer.

NOTE: Newer imx-kernels (5.15 and above) come with an entirely new Amphion VPU driver that
works correctly, obviating the need for these special elements.

To enable/disable the Amphion decoder elements, use the `v4l2-amphion` Meson feature option.

Also, the i.MX8 QuadMax/QuadXPlus SoCs contain the ISI (Image Sensing Interface), which can be
used for colorspace conversions and downscaling (but not upscaling). This functionality is
available through the V4L2 memory-to-memory API. But, like with the Amphion VPU driver situation,
the support for it in imx-kernels <= 5.4 is plagued with bugs, thus requiring patches on top
of GStreamer - or, as it is the case here, a custom transform element that takes care of
applying bug workarounds. And, just like with the VPU driver, newer imx-kernels (>= 5.15)
have an ISI mem-2-mem driver that works correctly, so this is mostly interesting for older
systems. The transform element is called `imxv4l2isivideotransform`, and can be enabled and
disabled by setting the `v4l2-isi` Meson option to `true` or `false`, respectively.


Other elements
--------------

* `imxuniaudiodec` : audio decoder plugin based on NXP's unified audio (UniAudio) architecture
* `imxmp3audioenc` : MP3 audio encoder plugin based on NXP's MP3 encoder


Integration with GStreamer and other external elements
------------------------------------------------------

All DMA buffer allocators that are used by the elements implement the [GstPhysMemoryAllocator interface](https://gstreamer.freedesktop.org/data/doc/gstreamer/stable/gst-plugins-base-libs/html/gst-plugins-base-libs-GstPhysMemoryAllocator.html).
This makes it possible to integrate them with other GStreamer elements, as long as said elements can make
use of the GstPhysMemory functionality to extract physical addresses. In addition, if the i.MX Soc BSP
supports ION based DMA buffer allocation, the elements will use an allocator based on
[GstDmaBufAllocator](https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-libs/html/gst-plugins-base-libs-dmabuf.html).
This allocator also implemnts the GstPhysMemoryAllocator interface, so with this, it is possible to
access the underlying DMA buffer by its DMA-BUF file descriptor and also by its physical address, making
integration even easier.

This is also why the old `imxeglvivsink` element from the earlier "v1" branch is not present anymore -
it is now unnecessary, since `glimagesink` and `waylandsink` both are full replacements.

Furthermore, if applications or other GStreamer elements wish to access some of the gstreamer-imx specific
functionality (particularly its allocators and interfaces), the gstimxcommon library can be used for this
purpose, since its API is public.


Dependencies
------------

* GStreamer version 1.14 or later; compositor elements and NV12_10LE40 format support require 1.16 or later,
  and support for the Hantro G2 style NV12_4L4 tiled format requires 1.18 or later
* [libimxdmabuffer](https://github.com/Freescale/libimxdmabuffer) version 1.1.2 or later
* [libimxvpuapi](https://github.com/Freescale/libimxvpuapi) version 2.2.2 or later

Also, the `videoparsersbad` plugin from the `gst-plugins-bad` package in GStreamer is needed, since
this plugin contains video parsers like `h265parse`, `h264parse`, `mpegvideoparse` (for MPEG1 and MPEG2),
and `mpeg4videoparse` (for MPEG4).

You must also use a Linux kernel with i.MX additions for i.MX specific subsystems such as the VPU, IPU, PxP etc.
Such kernels are typically referred to as "imx-kernels".


Building and installing
-----------------------

gstreamer-imx uses [meson](https://mesonbuild.com) as its build system. Amongst other reasons, this makes
integration with existing GStreamer build setups easier, such as [Cerbero](https://gitlab.freedesktop.org/gstreamer/cerbero).

At least version 0.53.2 is required. (Older versions can (partially) work, but build scripts may require changes/workarounds.) Versions prior to 0.54.0 require passing the `-Dsysroot` option; with Meson 0.54.0 and above,
using `-Dsysroot` is optional. It allows for explicitly specifying the sysroot path.

First, set up [the necessary cross compilation configuration for meson](https://mesonbuild.com/Cross-compilation.html).

Then, create a build directory for an out-of-tree build:

    make build
    cd build

Now set up the build by running meson:

    meson ..

You might want to look into the `--buildtype=plain` flag if the compiler flags Meson adds are a problem.
This is particularly useful for packagers. [Read here for more.](https://mesonbuild.com/Quick-guide.html#using-meson-as-a-distro-packager)

Also, you might be interested in the `-Dprefix` and `-Dlibdir` arguments to control where to install the
resulting binaries. Again, this is particularly useful for packagers.

Finally, build and install the code by running ninja:

    ninja install

### Build options

The following meson configuration options are available. Use them with the `-D` flag. For example, to
set the `vpu` option to "true", specify `-Dvpu=enabled`.

See the [Meson documentation about build option types](https://mesonbuild.com/Build-options.html#build-option-types)
to understand what values can be passed to these options.

Since most options are of type `feature`, this is the type in use unless a different
type is explicitly defined in the list below.

* `vpu`: VPU en/decoder elements.
* `uniaudiodec`: The `imxuniaudiodec` element.
* `mp3encoder`: The `imxmp3audioenc` element.
* `g2d`: 2D blitter elements based on the Vivante G2D API.
* `g2d-based-on-dpu`: Special option for use with i.MX8 QuadMax and QuadPlus SoCs. G2D
  is emulated via the DPU on these machines. The DPU behaves somewhat differently.
  It is recommended to set this option to `true` on these SoCs for better performance.
  On all other SoCs, this _must_ be set to `false` (the default value). Type: `boolean`.
* `ipu`: 2D blitter elements based on the NXP Image Processing Unit (IPU).
* `pxp`: 2D blitter elements based on the NXP Pixel Pipeline (PxP).
* `imx-headers-path`: Path to extra imx kernel headers. These are used for IPU and PxP
  code. The build scripts attempt to autodetect this path, so specifying this typically
  is not necessary. Type: `string`.
* `imx2d-videosink`: Enables/disables building 2D blitter video sink elements.
  Default value is `true`. Setting this to `false` makes sense on i.MX8 machines,
  since rendering to the framebuffer is not possible on those. Type: `boolean`.
* `imx2d-compositor`: Enables/disables building 2D blitter compositor elements.
  Type: `boolean`.
* `v4l2-mxc-source-sink`: Enables/disables building the custom Video4Linux2
  source / sink elements. See the Video4Linux2 section above for details. Type: `boolean`.
* `v4l2-isi`: Enables/disables building the custom Video4Linux2 video transform element
  that uses the ISI mem-2-mem device. See the Video4Linux2 section above for details.
  Type: `boolean`.
* `v4l2-amphion`: Enables/disables building the custom Video4Linux2 Amphion Malone VPU
  decoder that uses the Amphion mem-2-mem device. See the Video4Linux2 section above for
  details. Type: `feature`.
* `package-name`: GStreamer package name to use in the plugins. Type: `string`.
* `package-origin`: GStreamer package origin to use in the plugins. Type: `string`.


TODO
----

* Android support
* Debian packaging support
* More documentation in wiki
