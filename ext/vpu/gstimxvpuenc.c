/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2019  Carlos Rafael Giani
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <gst/video/gstvideometa.h>
#include <imxdmabuffer/imxdmabuffer.h>
#include <imxdmabuffer/imxdmabuffer_config.h>

#include <config.h>

#include "gst/imx/common/gstimxdmabufferallocator.h"
#include "gstimxvpucommon.h"
#include "gstimxvpuenc.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_enc_debug);
#define GST_CAT_DEFAULT imx_vpu_enc_debug


/* This is the base class for encoder elements. Derived classes
 * are implemented manually, unlike decoder ones. This is because
 * encoders typically have additional GObject properties that are
 * format specific, so autogenerating these subclasses (as it is
 * done for decoders) would not work.
 *
 * Still, some of the setup is done automatically in the base
 * class. To that end, the libimxvpuapi compression format enum
 * is stored as qdata in the derived classes. That qdata is
 * accessed using gst_imx_vpu_compression_format_quark().
 *
 * Furthermore, encoders do have common parameters, but they
 * differ slightly between formats. One example is the
 * quantization parameter, whose valid range depends on the
 * format. FOr this reason, not all common GObject properties
 * can be added to the GstImxVpuEnc base class, and instead must
 * be added to the subclasses directly. For this reason, there
 * are functions that must be called in the _class_init() and
 * _init() functions of the subclasses. These functions are
 * gst_imx_vpu_enc_common_class_init() and
 * gst_imx_vpu_enc_common_init(). */


enum
{
	PROP_0,
	PROP_GOP_SIZE,
	PROP_CLOSED_GOP_INTERVAL,
	PROP_BITRATE,
	PROP_QUANTIZATION,
	PROP_INTRA_REFRESH,
	PROP_FIXED_INTRA_QUANTIZATION,
	PROP_ALLOW_FRAMESKIPPING,
	PROP_USE_INTRA_REFRESH,
	PROP_INTRA_QP_BIAS,
	PROP_HRD_BUFFER_SIZE,
	PROP_USE_HRD,
	PROP_QP_MIN,
	PROP_QP_MIN_INTRA,
	PROP_STATIC_SCENE_IBIT_PERCENT,
	PROP_GDR_REFRESH_PERIOD,
	PROP_ROTATION,
	PROP_RATE_CONTROL,
	PROP_QP_MAX,
	PROP_QP_MAX_INTRA,
	PROP_INTRA_REFRESH_PERIOD,
	PROP_INTRA_REFRESH_DURATION,
	PROP_INTRA_REFRESH_ROWS,
	PROP_SLICE_HEIGHT,
	PROP_SLICE_COUNT,
	PROP_USE_ROLLING_SLICES,
	PROP_USE_ROLLING_TILES,
	PROP_ROLL_SIZE
};


#define DEFAULT_GOP_SIZE                 16
#define DEFAULT_CLOSED_GOP_INTERVAL      0
#define DEFAULT_BITRATE                  0
#define DEFAULT_INTRA_REFRESH            FALSE
#define DEFAULT_FIXED_INTRA_QUANTIZATION 0
#define DEFAULT_ALLOW_FRAMESKIPPING      FALSE
#define DEFAULT_USE_INTRA_REFRESH        FALSE
#define DEFAULT_INTRA_QP_BIAS			 0
#define DEFAULT_HRD_BUFFER_SIZE			 1000
#define DEFAULT_USE_HRD					 FALSE
#define DEFAULT_QP_MIN                  0
#define DEFAULT_QP_MIN_INTRA            0
#define DEFAULT_STATIC_SCENE_IBIT_PERCENT 0
#define DEFAULT_GDR_REFRESH_PERIOD      0
#define DEFAULT_ROTATION                0
#define DEFAULT_RATE_CONTROL            0
#define DEFAULT_QP_MAX                  0
#define DEFAULT_QP_MAX_INTRA            0
#define DEFAULT_INTRA_REFRESH_PERIOD    0
#define DEFAULT_INTRA_REFRESH_DURATION  0
#define DEFAULT_INTRA_REFRESH_ROWS      0
#define DEFAULT_SLICE_HEIGHT            0
#define DEFAULT_SLICE_COUNT             0
#define DEFAULT_USE_ROLLING_SLICES      0
#define DEFAULT_USE_ROLLING_TILES       0
#define DEFAULT_ROLL_SIZE               0


G_DEFINE_ABSTRACT_TYPE(GstImxVpuEnc, gst_imx_vpu_enc, GST_TYPE_VIDEO_ENCODER)


static void gst_imx_vpu_enc_dispose(GObject *object);

static void gst_imx_vpu_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_imx_vpu_enc_start(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_enc_stop(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state);
static GstFlowReturn gst_imx_vpu_enc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *cur_frame);
static GstFlowReturn gst_imx_vpu_enc_finish(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_enc_flush(GstVideoEncoder *encoder);
static gboolean gst_imx_vpu_enc_propose_allocation(GstVideoEncoder *encoder, GstQuery *query);

static gboolean gst_imx_vpu_enc_create_dma_buffer_pool(GstImxVpuEnc *imx_vpu_enc);
static void gst_imx_vpu_enc_free_fb_pool_dmabuffers(GstImxVpuEnc *imx_vpu_enc);
static GstFlowReturn gst_imx_vpu_enc_encode_queued_frames(GstImxVpuEnc *imx_vpu_enc);
static void gst_imx_vpu_enc_finalize(GObject *object);
static void gst_imx_vpu_enc_request_intra_region(GstImxVpuEnc *imx_vpu_enc, guint first_ctb_row, guint num_ctb_rows);


static void gst_imx_vpu_enc_class_init(GstImxVpuEncClass *klass)
{
	GObjectClass *object_class;
	GstVideoEncoderClass *video_encoder_class;

	gst_imx_vpu_api_setup_logging();

	GST_DEBUG_CATEGORY_INIT(imx_vpu_enc_debug, "imxvpuenc", 0, "NXP i.MX VPU video encoder");

	object_class = G_OBJECT_CLASS(klass);
	video_encoder_class = GST_VIDEO_ENCODER_CLASS(klass);

	object_class->dispose                   = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_dispose);
	object_class->finalize                  = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_finalize);

	klass->request_intra_region = gst_imx_vpu_enc_request_intra_region;

	g_signal_new(
		"request-intra-region",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		G_STRUCT_OFFSET(GstImxVpuEncClass, request_intra_region),
		NULL, NULL, NULL,
		G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT
	);

	video_encoder_class->start              = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_start);
	video_encoder_class->stop               = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_stop);
	video_encoder_class->set_format         = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_set_format);
	video_encoder_class->handle_frame       = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_handle_frame);
	video_encoder_class->finish             = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_finish);
	video_encoder_class->flush              = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_flush);
	video_encoder_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_propose_allocation);
}


static void gst_imx_vpu_enc_init(GstImxVpuEnc *imx_vpu_enc)
{
	imx_vpu_enc->gop_size = DEFAULT_GOP_SIZE;
	imx_vpu_enc->closed_gop_interval = DEFAULT_CLOSED_GOP_INTERVAL;
	imx_vpu_enc->bitrate = DEFAULT_BITRATE;
	imx_vpu_enc->intra_refresh = DEFAULT_INTRA_REFRESH;
	imx_vpu_enc->fixed_intra_quantization = DEFAULT_FIXED_INTRA_QUANTIZATION;
	imx_vpu_enc->allow_frameskipping = DEFAULT_ALLOW_FRAMESKIPPING;
	imx_vpu_enc->use_intra_refresh = DEFAULT_USE_INTRA_REFRESH;
	imx_vpu_enc->intra_qp_bias = DEFAULT_INTRA_QP_BIAS;
	imx_vpu_enc->hrd_buffer_size = DEFAULT_HRD_BUFFER_SIZE;
	imx_vpu_enc->use_hrd = DEFAULT_USE_HRD;
	imx_vpu_enc->qp_min = DEFAULT_QP_MIN;
	imx_vpu_enc->qp_min_intra = DEFAULT_QP_MIN_INTRA;
	imx_vpu_enc->static_scene_ibit_percent = DEFAULT_STATIC_SCENE_IBIT_PERCENT;
	imx_vpu_enc->gdr_refresh_period = DEFAULT_GDR_REFRESH_PERIOD;
	imx_vpu_enc->rotation = DEFAULT_ROTATION;
	imx_vpu_enc->rate_control = DEFAULT_RATE_CONTROL;
	imx_vpu_enc->qp_max = DEFAULT_QP_MAX;
	imx_vpu_enc->qp_max_intra = DEFAULT_QP_MAX_INTRA;
	imx_vpu_enc->intra_refresh_period = DEFAULT_INTRA_REFRESH_PERIOD;
	imx_vpu_enc->intra_refresh_duration = DEFAULT_INTRA_REFRESH_DURATION;
	imx_vpu_enc->intra_refresh_rows = DEFAULT_INTRA_REFRESH_ROWS;
	imx_vpu_enc->slice_height = DEFAULT_SLICE_HEIGHT;
	imx_vpu_enc->slice_count = DEFAULT_SLICE_COUNT;
	imx_vpu_enc->use_rolling_slices = DEFAULT_USE_ROLLING_SLICES;
	imx_vpu_enc->use_rolling_tiles = DEFAULT_USE_ROLLING_TILES;
	imx_vpu_enc->roll_size = DEFAULT_ROLL_SIZE;

	imx_vpu_enc->stream_buffer = NULL;
	imx_vpu_enc->encoder = NULL;
	imx_vpu_enc->enc_global_info = imx_vpu_api_enc_get_global_info();
	memset(&(imx_vpu_enc->open_params), 0, sizeof(imx_vpu_enc->open_params));
	imx_vpu_enc->default_dma_buf_allocator = NULL;

	imx_vpu_enc->dma_buffer_pool = NULL;
	imx_vpu_enc->uploader = NULL;
	imx_vpu_enc->uploaded_buffers_table = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)gst_buffer_unref);
	imx_vpu_enc->fb_pool_buffers = NULL;

	imx_vpu_enc->cached_headers = NULL;
	imx_vpu_enc->cached_headers_size = 0;
	imx_vpu_enc->output_frame_count = 0;
	imx_vpu_enc->config_interval = 0;
	imx_vpu_enc->config_interval_frames = 0;

	imx_vpu_enc->fatal_error_cannot_encode = FALSE;

	g_mutex_init(&(imx_vpu_enc->intra_region_mutex));
	imx_vpu_enc->intra_region_q_count = 0;
}


static void gst_imx_vpu_enc_dispose(GObject *object)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(object);

	if (imx_vpu_enc->uploaded_buffers_table != NULL)
	{
		g_hash_table_remove_all(imx_vpu_enc->uploaded_buffers_table);
		g_hash_table_unref(imx_vpu_enc->uploaded_buffers_table);
		imx_vpu_enc->uploaded_buffers_table = NULL;
	}

	G_OBJECT_CLASS(gst_imx_vpu_enc_parent_class)->dispose(object);
}


static void gst_imx_vpu_enc_finalize(GObject *object)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(object);

	g_mutex_clear(&(imx_vpu_enc->intra_region_mutex));

	G_OBJECT_CLASS(gst_imx_vpu_enc_parent_class)->finalize(object);
}


static void gst_imx_vpu_enc_request_intra_region(GstImxVpuEnc *imx_vpu_enc, guint first_ctb_row, guint num_ctb_rows)
{
	if (num_ctb_rows == 0)
		return;

	g_mutex_lock(&(imx_vpu_enc->intra_region_mutex));
	if (imx_vpu_enc->intra_region_q_count < GST_IMX_VPU_ENC_INTRA_REGION_QUEUE_SIZE)
	{
		int i = imx_vpu_enc->intra_region_q_count;
		imx_vpu_enc->intra_region_q_first[i] = first_ctb_row;
		imx_vpu_enc->intra_region_q_num[i] = num_ctb_rows;
		imx_vpu_enc->intra_region_q_count++;
	}
	g_mutex_unlock(&(imx_vpu_enc->intra_region_mutex));

	GST_DEBUG_OBJECT(imx_vpu_enc, "request-intra-region: CTB rows %u..%u",
		first_ctb_row, first_ctb_row + num_ctb_rows - 1);
}


/* Say it once per element and per property, at a level a normal log shows.
 * Glib only complains about G_PARAM_DEPRECATED when G_ENABLE_DIAGNOSTIC is
 * set, which nothing on the target sets, so a pipeline could go on using a
 * deprecated property indefinitely without anything saying so.
 *
 * Only when the value actually selects the deprecated behaviour. Plenty of
 * callers set every property they know about, including to the default that
 * means "off", and warning about those would be noise that trains people to
 * ignore the ones that matter. */
static void gst_imx_vpu_enc_warn_deprecated(GstImxVpuEnc *imx_vpu_enc, guint bit,
                                            gboolean in_use,
                                            gchar const *old_name, gchar const *new_name)
{
	if (!in_use || (imx_vpu_enc->deprecation_warned & (1u << bit)))
		return;

	imx_vpu_enc->deprecation_warned |= (1u << bit);
	GST_WARNING_OBJECT(imx_vpu_enc, "the \"%s\" property is deprecated; use %s instead",
	                   old_name, new_name);
}


static void gst_imx_vpu_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(object);
	GstImxVpuEncClass *klass = GST_IMX_VPU_ENC_CLASS(G_OBJECT_GET_CLASS(object));

	switch (prop_id)
	{
		case PROP_GOP_SIZE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->gop_size = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_CLOSED_GOP_INTERVAL:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->closed_gop_interval = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_BITRATE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->bitrate = g_value_get_uint(value);
			if (imx_vpu_enc->encoder != NULL)
				imx_vpu_api_enc_set_bitrate(imx_vpu_enc->encoder, imx_vpu_enc->bitrate);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QUANTIZATION:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->quantization = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_INTRA_REFRESH:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->intra_refresh = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_INTRA_REFRESH_PERIOD:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->intra_refresh_period = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_INTRA_REFRESH_DURATION:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->intra_refresh_duration = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_INTRA_REFRESH_ROWS:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->intra_refresh_rows = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_SLICE_HEIGHT:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->slice_height = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_SLICE_COUNT:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->slice_count = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_USE_ROLLING_SLICES:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->use_rolling_slices = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			gst_imx_vpu_enc_warn_deprecated(imx_vpu_enc, 1, imx_vpu_enc->use_rolling_slices != 0,
			                                "use-rolling-slices",
			                                "intra-refresh with intra-refresh-rows and slice-count");
			break;

		case PROP_USE_ROLLING_TILES:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->use_rolling_tiles = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			gst_imx_vpu_enc_warn_deprecated(imx_vpu_enc, 2, imx_vpu_enc->use_rolling_tiles != 0,
			                                "use-rolling-tiles", "intra-refresh");
			break;

		case PROP_ROLL_SIZE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->roll_size = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			gst_imx_vpu_enc_warn_deprecated(imx_vpu_enc, 3, imx_vpu_enc->roll_size != 0,
			                                "roll-size", "intra-refresh-period");
			break;

		case PROP_FIXED_INTRA_QUANTIZATION:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->fixed_intra_quantization = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_ALLOW_FRAMESKIPPING:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->allow_frameskipping = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_USE_INTRA_REFRESH:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->use_intra_refresh = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			gst_imx_vpu_enc_warn_deprecated(imx_vpu_enc, 0, imx_vpu_enc->use_intra_refresh,
			                                "use-intra-refresh", "intra-refresh");
			break;

		case PROP_INTRA_QP_BIAS:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->intra_qp_bias = g_value_get_int(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_HRD_BUFFER_SIZE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->hrd_buffer_size = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_USE_HRD:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->use_hrd = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QP_MIN:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->qp_min = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QP_MIN_INTRA:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->qp_min_intra = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_STATIC_SCENE_IBIT_PERCENT:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->static_scene_ibit_percent = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_GDR_REFRESH_PERIOD:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->gdr_refresh_period = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			gst_imx_vpu_enc_warn_deprecated(imx_vpu_enc, 4, imx_vpu_enc->gdr_refresh_period != 0,
			                                "gdr-refresh-period", "intra-refresh-period");
			break;

		case PROP_ROTATION:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->rotation = g_value_get_uint(value);
		case PROP_RATE_CONTROL:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->rate_control = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QP_MAX:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->qp_max = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QP_MAX_INTRA:
			GST_OBJECT_LOCK(imx_vpu_enc);
			imx_vpu_enc->qp_max_intra = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		default:
			if (klass->set_encoder_property != NULL)
				klass->set_encoder_property(object, prop_id, value, pspec);
			else
				G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_vpu_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(object);
	GstImxVpuEncClass *klass = GST_IMX_VPU_ENC_CLASS(G_OBJECT_GET_CLASS(object));

	switch (prop_id)
	{
		case PROP_GOP_SIZE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->gop_size);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_CLOSED_GOP_INTERVAL:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->closed_gop_interval);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_BITRATE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->bitrate);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QUANTIZATION:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->quantization);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_INTRA_REFRESH:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_boolean(value, imx_vpu_enc->intra_refresh);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_INTRA_REFRESH_PERIOD:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->intra_refresh_period);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_INTRA_REFRESH_DURATION:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->intra_refresh_duration);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_INTRA_REFRESH_ROWS:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->intra_refresh_rows);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_SLICE_HEIGHT:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->slice_height);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_SLICE_COUNT:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->slice_count);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_USE_ROLLING_SLICES:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->use_rolling_slices);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_USE_ROLLING_TILES:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->use_rolling_tiles);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_ROLL_SIZE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->roll_size);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_FIXED_INTRA_QUANTIZATION:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->fixed_intra_quantization);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_ALLOW_FRAMESKIPPING:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_boolean(value, imx_vpu_enc->allow_frameskipping);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_USE_INTRA_REFRESH:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_boolean(value, imx_vpu_enc->use_intra_refresh);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_INTRA_QP_BIAS:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_int(value, imx_vpu_enc->intra_qp_bias);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_HRD_BUFFER_SIZE:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->hrd_buffer_size);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_USE_HRD:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_boolean(value, imx_vpu_enc->use_hrd);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QP_MIN:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->qp_min);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QP_MIN_INTRA:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->qp_min_intra);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_STATIC_SCENE_IBIT_PERCENT:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->static_scene_ibit_percent);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_GDR_REFRESH_PERIOD:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->gdr_refresh_period);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_ROTATION:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->rotation);
		case PROP_RATE_CONTROL:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->rate_control);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QP_MAX:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->qp_max);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		case PROP_QP_MAX_INTRA:
			GST_OBJECT_LOCK(imx_vpu_enc);
			g_value_set_uint(value, imx_vpu_enc->qp_max_intra);
			GST_OBJECT_UNLOCK(imx_vpu_enc);
			break;

		default:
			if (klass->get_encoder_property != NULL)
				klass->get_encoder_property(object, prop_id, value, pspec);
			else
				G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static gboolean gst_imx_vpu_enc_start(GstVideoEncoder *encoder)
{
	gboolean ret = TRUE;
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(encoder);
	GstImxVpuEncClass *klass = GST_IMX_VPU_ENC_CLASS(G_OBJECT_GET_CLASS(encoder));
	size_t stream_buffer_size, stream_buffer_alignment;
	GstAllocationParams alloc_params;
	ImxVpuApiCompressionFormat compression_format = GST_IMX_VPU_GET_ELEMENT_COMPRESSION_FORMAT(encoder);
	GstImxVpuCodecDetails const * codec_details = gst_imx_vpu_get_codec_details(compression_format);

	imx_vpu_enc->fatal_error_cannot_encode = FALSE;

	imx_vpu_enc->keyframe_type = klass->use_idr_frame_type_for_keyframes ? IMX_VPU_API_FRAME_TYPE_IDR : IMX_VPU_API_FRAME_TYPE_I;

	stream_buffer_size = imx_vpu_enc->enc_global_info->min_required_stream_buffer_size;
	stream_buffer_alignment = imx_vpu_enc->enc_global_info->required_stream_buffer_physaddr_alignment;

	GST_DEBUG_OBJECT(
		imx_vpu_enc,
		"stream buffer info:  required min size: %zu bytes  required alignment: %zu",
		stream_buffer_size,
		stream_buffer_alignment
	);

	memset(&alloc_params, 0, sizeof(alloc_params));
	alloc_params.align = stream_buffer_alignment - 1;

	imx_vpu_enc->default_dma_buf_allocator = gst_imx_allocator_new();

	imx_vpu_enc->uploader = gst_imx_dma_buffer_uploader_new(imx_vpu_enc->default_dma_buf_allocator);

	if (stream_buffer_size > 0)
	{
		imx_vpu_enc->stream_buffer = gst_allocator_alloc(
			imx_vpu_enc->default_dma_buf_allocator,
			stream_buffer_size,
			&alloc_params
		);
		if (G_UNLIKELY(imx_vpu_enc->stream_buffer == NULL))
		{
			GST_ELEMENT_ERROR(imx_vpu_enc, RESOURCE, FAILED, ("could not allocate DMA memory for stream buffer"), (NULL));
			ret = FALSE;
			goto finish;
		}
	}
	else
		GST_DEBUG_OBJECT(imx_vpu_enc, "not allocating stream buffer since the VPU does not need one");

	/* VPU encoder setup continues in set_format(), since we need to
	 * know the input caps to fill the open_params structure. */

	GST_INFO_OBJECT(imx_vpu_enc, "i.MX VPU %s encoder started", codec_details->desc_name);


finish:
	return ret;
}


static gboolean gst_imx_vpu_enc_stop(GstVideoEncoder *encoder)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(encoder);
	ImxVpuApiCompressionFormat compression_format = GST_IMX_VPU_GET_ELEMENT_COMPRESSION_FORMAT(encoder);
	GstImxVpuCodecDetails const * codec_details = gst_imx_vpu_get_codec_details(compression_format);

	g_hash_table_remove_all(imx_vpu_enc->uploaded_buffers_table);

	if (imx_vpu_enc->uploader != NULL)
	{
		gst_object_unref(GST_OBJECT(imx_vpu_enc->uploader));
		imx_vpu_enc->uploader = NULL;
	}

	if (imx_vpu_enc->encoder != NULL)
	{
		imx_vpu_api_enc_close(imx_vpu_enc->encoder);
		imx_vpu_enc->encoder = NULL;
	}

	gst_imx_vpu_enc_free_fb_pool_dmabuffers(imx_vpu_enc);

	if (imx_vpu_enc->dma_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(imx_vpu_enc->dma_buffer_pool));
		imx_vpu_enc->dma_buffer_pool = NULL;
	}

	if (imx_vpu_enc->stream_buffer != NULL)
	{
		gst_memory_unref(imx_vpu_enc->stream_buffer);
		imx_vpu_enc->stream_buffer = NULL;
	}

	if (imx_vpu_enc->default_dma_buf_allocator != NULL)
	{
		gst_object_unref(GST_OBJECT(imx_vpu_enc->default_dma_buf_allocator));
		imx_vpu_enc->default_dma_buf_allocator = NULL;
	}

	g_free(imx_vpu_enc->cached_headers);
	imx_vpu_enc->cached_headers = NULL;
	imx_vpu_enc->cached_headers_size = 0;
	imx_vpu_enc->output_frame_count = 0;

	GST_INFO_OBJECT(imx_vpu_enc, "i.MX VPU %s encoder stopped", codec_details->desc_name);

	return TRUE;
}


static gboolean gst_imx_vpu_enc_set_format(GstVideoEncoder *encoder, GstVideoCodecState *state)
{
	ImxVpuApiEncReturnCodes enc_ret;
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC(encoder);
	GstImxVpuEncClass *klass = GST_IMX_VPU_ENC_CLASS(G_OBJECT_GET_CLASS(encoder));
	gboolean ret = TRUE;
	guint rotation;
	GstVideoFormat video_format;
	ImxVpuApiEncOpenParams *open_params = &(imx_vpu_enc->open_params);
	ImxVpuApiCompressionFormat compression_format = GST_IMX_VPU_GET_ELEMENT_COMPRESSION_FORMAT(encoder);
	ImxVpuApiColorFormat color_format;
	GstCaps *output_caps;
	GstVideoCodecState *output_state;
	ImxVpuApiEncSessionState session_state;
	gboolean have_session_state = FALSE;

	// TODO: Communicate alignment information from ImxVpuApiEncGlobalInfo to upstream somehow

	g_assert(klass->get_output_caps != NULL);

	GST_DEBUG_OBJECT(encoder, "setting encoder format");


	if (imx_vpu_enc->encoder != NULL)
	{
		/* This is where a resolution change lands: the encoder is per frame
		 * size, so a new one has to be opened. The stream itself continues
		 * though, and two things about it must not restart with the new
		 * encoder - the rate control's buffer level, which describes the link
		 * and not the picture size, and the parameter set ids, which a
		 * decoder uses to tell the resolutions apart. Carry them over. */
		if (imx_vpu_api_enc_get_session_state(imx_vpu_enc->encoder, &session_state) == IMX_VPU_API_ENC_RETURN_CODE_OK)
			have_session_state = TRUE;

		imx_vpu_api_enc_close(imx_vpu_enc->encoder);
		imx_vpu_enc->encoder = NULL;
	}

	g_hash_table_remove_all(imx_vpu_enc->uploaded_buffers_table);

	gst_imx_vpu_enc_free_fb_pool_dmabuffers(imx_vpu_enc);

	if (imx_vpu_enc->dma_buffer_pool != NULL)
	{
		gst_object_unref(imx_vpu_enc->dma_buffer_pool);
		imx_vpu_enc->dma_buffer_pool = NULL;
	}

	g_free(imx_vpu_enc->cached_headers);
	imx_vpu_enc->cached_headers = NULL;
	imx_vpu_enc->cached_headers_size = 0;
	imx_vpu_enc->output_frame_count = 0;


	/* Begin filling the open_params. */

	imx_vpu_enc->in_video_info = state->info;

	video_format = GST_VIDEO_INFO_FORMAT(&(state->info));
	if (!gst_imx_vpu_color_format_from_gstvidfmt(&color_format, video_format))
	{
		GST_ERROR_OBJECT(encoder, "unsupported color format %s", gst_video_format_to_string(video_format));
		ret = FALSE;
		goto finish;
	}

	memset(open_params, 0, sizeof(ImxVpuApiEncOpenParams));
	imx_vpu_api_enc_set_default_open_params(
		compression_format,
		color_format,
		GST_VIDEO_INFO_WIDTH(&(state->info)),
		GST_VIDEO_INFO_HEIGHT(&(state->info)),
		open_params
	);

	open_params->frame_rate_numerator = GST_VIDEO_INFO_FPS_N(&(state->info));
	open_params->frame_rate_denominator = GST_VIDEO_INFO_FPS_D(&(state->info));

	GST_OBJECT_LOCK(imx_vpu_enc);
	open_params->bitrate = imx_vpu_enc->bitrate;
	open_params->gop_size = imx_vpu_enc->gop_size;
	open_params->closed_gop_interval = imx_vpu_enc->closed_gop_interval;
	open_params->quantization = imx_vpu_enc->quantization;
	/* Cyclic intra refresh. Never reachable any more: the property that fed
	 * it was a deprecated macroblock count that could not produce a sweep -
	 * cirStart stayed at 0, so it re-coded the same CTBs in every picture -
	 * and its name now belongs to the intra refresh switch. */
	open_params->min_intra_refresh_mb_count = 0;
	open_params->fixed_intra_quantization = imx_vpu_enc->fixed_intra_quantization;
	open_params->flags = (imx_vpu_enc->allow_frameskipping ? IMX_VPU_API_ENC_OPEN_PARAMS_FLAG_ALLOW_FRAMESKIPPING : 0)
	                   | ((imx_vpu_enc->intra_refresh || imx_vpu_enc->use_intra_refresh) ? IMX_VPU_API_ENC_OPEN_PARAMS_FLAG_USE_INTRA_REFRESH : 0);
	open_params->flags |= imx_vpu_enc->use_hrd ? IMX_VPU_API_ENC_H26x_OPEN_PARAMS_FLAG_USE_HRD : 0;
	open_params->intra_qp_delta = imx_vpu_enc->intra_qp_bias;
	open_params->hrd_buffer_size = imx_vpu_enc->hrd_buffer_size;
	open_params->qp_min_inter = imx_vpu_enc->qp_min;
	open_params->qp_min_intra = imx_vpu_enc->qp_min_intra;
	open_params->static_scene_ibit_percent = imx_vpu_enc->static_scene_ibit_percent;
	open_params->intra_refresh_period = imx_vpu_enc->intra_refresh_period;
	open_params->intra_refresh_duration = imx_vpu_enc->intra_refresh_duration;
	open_params->intra_refresh_rows = imx_vpu_enc->intra_refresh_rows;
	open_params->intra_refresh_columns = 0;
	open_params->slice_height = imx_vpu_enc->slice_height;
	open_params->slice_count = imx_vpu_enc->slice_count;
	/* The deprecated properties are passed through rather than translated
	 * here: the library maps them, so every caller of it - the element, the
	 * tools, anything else - gets the same mapping. */
	open_params->gdr_refresh_period = imx_vpu_enc->gdr_refresh_period;
	open_params->rotation_180 = (imx_vpu_enc->rotation == 180);
	rotation = imx_vpu_enc->rotation;
	open_params->num_rolling_slices = imx_vpu_enc->use_rolling_slices;
	open_params->num_rolling_tiles = imx_vpu_enc->use_rolling_tiles;
	open_params->roll_size = imx_vpu_enc->roll_size;
	open_params->rate_control_mode = imx_vpu_enc->rate_control;
	open_params->qp_max_inter = imx_vpu_enc->qp_max;
	open_params->qp_max_intra = imx_vpu_enc->qp_max_intra;
	GST_OBJECT_UNLOCK(imx_vpu_enc);

	if ((rotation != 0) && (rotation != 180))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "unsupported rotation %u; the encoder pre-processor supports 0 and 180 only", rotation);
		ret = FALSE;
		goto finish;
	}

	GST_DEBUG_OBJECT(encoder, "setting bitrate to %u kbps and GOP size to %u", open_params->bitrate, open_params->gop_size);
	if (open_params->flags & IMX_VPU_API_ENC_OPEN_PARAMS_FLAG_USE_INTRA_REFRESH)
		GST_DEBUG_OBJECT(encoder, "intra refresh on: period %u, duration %u, %u CTB row(s) per band",
		                 open_params->intra_refresh_period, open_params->intra_refresh_duration,
		                 open_params->intra_refresh_rows);


	/* Let the subclass fill the format specific open params. */
	if ((klass->set_open_params != NULL) && !(klass->set_open_params(imx_vpu_enc, open_params)))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not set compression format specific open params");
		ret = FALSE;
		goto finish;
	}


	/* Open and configure encoder. */
	if ((enc_ret = imx_vpu_api_enc_open(
		&(imx_vpu_enc->encoder),
		&(imx_vpu_enc->open_params),
		(imx_vpu_enc->stream_buffer != NULL) ? gst_imx_get_dma_buffer_from_memory(imx_vpu_enc->stream_buffer) : NULL
	)) != IMX_VPU_API_ENC_RETURN_CODE_OK)
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not open encoder: %s", imx_vpu_api_enc_return_code_string(enc_ret));
		ret = FALSE;
		goto finish;
	}


	/* Continue the stream the encoder that was just closed was producing. */
	if (have_session_state)
	{
		ImxVpuApiEncReturnCodes state_ret = imx_vpu_api_enc_set_session_state(imx_vpu_enc->encoder, &session_state);

		if (state_ret == IMX_VPU_API_ENC_RETURN_CODE_OK)
			GST_DEBUG_OBJECT(imx_vpu_enc, "continuing the stream of the previous encoder instance");
		else
			GST_WARNING_OBJECT(imx_vpu_enc, "could not continue the stream of the previous encoder instance: %s",
			                   imx_vpu_api_enc_return_code_string(state_ret));
	}


	/* Retrieve stream info. */
	{
		ImxVpuApiEncStreamInfo const *new_stream_info = imx_vpu_api_enc_get_stream_info(imx_vpu_enc->encoder);
		g_assert(new_stream_info != NULL);
		imx_vpu_enc->current_stream_info = *new_stream_info;
	}


	/* Get output caps from the subclass and set the output state. */

	if ((output_caps = klass->get_output_caps(imx_vpu_enc, &(imx_vpu_enc->current_stream_info))) == NULL)
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not get output caps");
		ret = FALSE;
		goto finish;
	}

	output_state = gst_video_encoder_set_output_state(encoder, output_caps, state);
	gst_video_codec_state_unref(output_state);


	/* Create DMA buffer pool that will be used for the encoder's
	 * framebuffer pool and for internal input buffers. */
	if (!gst_imx_vpu_enc_create_dma_buffer_pool(imx_vpu_enc))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not create DMA buffer pool");
		ret = FALSE;
		goto finish;
	}


	/* Allocate framebuffer pool buffers and register them with the VPU. */
	if (imx_vpu_enc->current_stream_info.min_num_required_framebuffers > 0)
	{
		gsize i;
		gsize num_buffers;
		ImxDmaBuffer **fb_dmabuffers;

		num_buffers = imx_vpu_enc->current_stream_info.min_num_required_framebuffers;
		imx_vpu_enc->fb_pool_buffers = gst_buffer_list_new_sized(num_buffers);

		for (i = 0; i < num_buffers; ++i)
		{
			GstBuffer *buffer = NULL;
			GstFlowReturn flow_ret;

			flow_ret = gst_buffer_pool_acquire_buffer(imx_vpu_enc->dma_buffer_pool, &buffer, NULL);
			if (flow_ret != GST_FLOW_OK)
			{
				GST_ERROR_OBJECT(imx_vpu_enc, "could not acquire DMA buffer: %s", gst_flow_get_name(flow_ret));
				ret = FALSE;
				goto finish;
			}

			gst_buffer_list_add(imx_vpu_enc->fb_pool_buffers, buffer);
		}

		fb_dmabuffers = g_slice_alloc(num_buffers * sizeof(ImxDmaBuffer *));
		for (i = 0; i < num_buffers; ++i)
			fb_dmabuffers[i] = gst_imx_get_dma_buffer_from_buffer(gst_buffer_list_get(imx_vpu_enc->fb_pool_buffers, i));
		enc_ret = imx_vpu_api_enc_add_framebuffers_to_pool(imx_vpu_enc->encoder, fb_dmabuffers, num_buffers);
		g_slice_free1(num_buffers * sizeof(ImxDmaBuffer *), fb_dmabuffers);

		if (enc_ret != IMX_VPU_API_ENC_RETURN_CODE_OK)
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "could not : add framebuffers to VPU pool: %s", imx_vpu_api_enc_return_code_string(enc_ret));
			ret = FALSE;
			goto finish;
		}
	}


finish:
	return ret;
}


static GstFlowReturn gst_imx_vpu_enc_handle_frame(GstVideoEncoder *encoder, GstVideoCodecFrame *cur_frame)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC_CAST(encoder);
	GstImxVpuEncClass *klass = GST_IMX_VPU_ENC_CLASS(G_OBJECT_GET_CLASS(encoder));
	GstFlowReturn flow_ret = GST_FLOW_OK;

	if (G_UNLIKELY(imx_vpu_enc->encoder == NULL))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "encoder was not initialized; cannot continue");
		flow_ret = GST_FLOW_ERROR;
		goto finish;
	}

	if (G_UNLIKELY(imx_vpu_enc->fatal_error_cannot_encode))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "fatal error previously recorded; cannot encode");
		flow_ret = GST_FLOW_ERROR;
		goto finish;
	}

	flow_ret = GST_FLOW_OK;

	if (G_LIKELY(cur_frame != NULL))
	{
		ImxDmaBuffer *fb_dma_buffer = NULL;
		ImxVpuApiRawFrame raw_frame;
		ImxVpuApiEncReturnCodes enc_ret;
		GstBuffer *uploaded_input_buffer;
		gboolean force_keyframe;

		GST_LOG_OBJECT(imx_vpu_enc, "about to prepare and queue frame with number #%" G_GUINT32_FORMAT " for encoding", cur_frame->system_frame_number);

		flow_ret = gst_imx_dma_buffer_uploader_perform(imx_vpu_enc->uploader, cur_frame->input_buffer, &uploaded_input_buffer);
		if (G_UNLIKELY(flow_ret != GST_FLOW_OK))
			goto finish;

		fb_dma_buffer = gst_imx_get_dma_buffer_from_buffer(uploaded_input_buffer);

		g_hash_table_insert(imx_vpu_enc->uploaded_buffers_table, (gpointer)(gintptr)(cur_frame->system_frame_number), uploaded_input_buffer);

		g_assert(fb_dma_buffer != NULL);

		raw_frame.fb_dma_buffer = fb_dma_buffer;
		raw_frame.frame_types[0] = raw_frame.frame_types[1] = IMX_VPU_API_FRAME_TYPE_UNKNOWN;
		raw_frame.pts = cur_frame->pts;
		raw_frame.dts = cur_frame->dts;
		/* The system frame number is necessary to correctly associate encoded
		 * frames and decoded frames. This is required, because some formats
		 * have a delay (= output frames only show up after N complete input
		 * frames), and others like h.264 even reorder frames. */
		raw_frame.context = (void *)((guintptr)(cur_frame->system_frame_number));

		if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME(cur_frame))
		{
			GST_LOG_OBJECT(
				imx_vpu_enc,
				"force-keyframe flag set; forcing VPU to encode this frame as an %s frame",
				klass->use_idr_frame_type_for_keyframes ? "IDR" : "I"
			);
			force_keyframe = TRUE;
		}
		else if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME_HEADERS(cur_frame))
		{
			GST_LOG_OBJECT(
				imx_vpu_enc,
				"force-keyframe-headers flag set; forcing VPU to encode this frame as an %s frame",
				klass->use_idr_frame_type_for_keyframes ? "IDR" : "I"
			);
			force_keyframe = TRUE;
		}
		else
			force_keyframe = FALSE;

		if (force_keyframe)
			raw_frame.frame_types[0] = klass->use_idr_frame_type_for_keyframes ? IMX_VPU_API_FRAME_TYPE_IDR : IMX_VPU_API_FRAME_TYPE_I;

		{
			guint qf[GST_IMX_VPU_ENC_INTRA_REGION_QUEUE_SIZE];
			guint qn[GST_IMX_VPU_ENC_INTRA_REGION_QUEUE_SIZE];
			int qc, qi;
			g_mutex_lock(&(imx_vpu_enc->intra_region_mutex));
			qc = imx_vpu_enc->intra_region_q_count;
			for (qi = 0; qi < qc; qi++)
			{
				qf[qi] = imx_vpu_enc->intra_region_q_first[qi];
				qn[qi] = imx_vpu_enc->intra_region_q_num[qi];
			}
			imx_vpu_enc->intra_region_q_count = 0;
			g_mutex_unlock(&(imx_vpu_enc->intra_region_mutex));
			for (qi = 0; qi < qc; qi++)
				imx_vpu_api_enc_set_intra_refresh_region(imx_vpu_enc->encoder, qf[qi], qn[qi]);
		}

		/* The actual encoding */
		if ((enc_ret = imx_vpu_api_enc_push_raw_frame(imx_vpu_enc->encoder, &raw_frame)) != IMX_VPU_API_ENC_RETURN_CODE_OK)
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "could not push raw frame into encoder: %s", imx_vpu_api_enc_return_code_string(enc_ret));

			flow_ret = GST_FLOW_ERROR;
			goto finish;
		}

		/* The GstVideoCodecFrame passed to handle_frame() gets ref'd prior
		 * to that call. Since we don't pass it directly to finish_frame()
		 * here (because we aren't done with it yet), we have to unref it
		 * here. We'll pull the frame from the GstVideoEncoder queue based
		 * on its system frame number later, and then we finish it.
		 * (We explicitely unref it here, even though the code below unrefs
		 * it as well if it is non-NULL. That's because this way, it is
		 * ensured that it is unref'd *before* encoding queued frames, thus
		 * making sure that buffers with encoded data are finished as soon
		 * as possible once downstream are done with them.) */
		gst_video_codec_frame_unref(cur_frame);
		cur_frame = NULL;
	}

	flow_ret = gst_imx_vpu_enc_encode_queued_frames(imx_vpu_enc);


finish:
	if (cur_frame != NULL)
		gst_video_codec_frame_unref(cur_frame);

	if (flow_ret == GST_FLOW_ERROR)
		imx_vpu_enc->fatal_error_cannot_encode = TRUE;

	return flow_ret;
}


static GstFlowReturn gst_imx_vpu_enc_finish(GstVideoEncoder *encoder)
{
	GstFlowReturn flow_ret;
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC_CAST(encoder);

	if (imx_vpu_enc->encoder == NULL)
		return GST_FLOW_OK;

	if (G_UNLIKELY(imx_vpu_enc->fatal_error_cannot_encode))
		return GST_FLOW_OK;

	imx_vpu_api_enc_enable_drain_mode(imx_vpu_enc->encoder);

	GST_INFO_OBJECT(imx_vpu_enc, "pushing out all remaining unfinished frames");

	flow_ret = gst_imx_vpu_enc_encode_queued_frames(imx_vpu_enc);
	if (flow_ret == GST_FLOW_EOS)
		flow_ret = GST_FLOW_OK;

	return flow_ret;
}


static gboolean gst_imx_vpu_enc_flush(GstVideoEncoder *encoder)
{
	GstImxVpuEnc *imx_vpu_enc = GST_IMX_VPU_ENC_CAST(encoder);

	if (imx_vpu_enc->encoder != NULL)
		imx_vpu_api_enc_flush(imx_vpu_enc->encoder);

	imx_vpu_enc->output_frame_count = 0;

	return TRUE;
}


static gboolean gst_imx_vpu_enc_propose_allocation(GstVideoEncoder *encoder, GstQuery *query)
{
	if (!GST_VIDEO_ENCODER_CLASS(gst_imx_vpu_enc_parent_class)->propose_allocation(encoder, query))
		return FALSE;

	/* Inform upstream that we can handle GstVideoMeta. */
	gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, 0);

	return TRUE;
}


static gboolean gst_imx_vpu_enc_create_dma_buffer_pool(GstImxVpuEnc *imx_vpu_enc)
{
	GstStructure *pool_config;
	GstAllocationParams alloc_params;
	gboolean ret = TRUE;

	g_assert(imx_vpu_enc->dma_buffer_pool == NULL);

	memset(&alloc_params, 0, sizeof(alloc_params));
	alloc_params.align = imx_vpu_enc->current_stream_info.framebuffer_alignment;
	if (alloc_params.align > 0)
		alloc_params.align--;

	imx_vpu_enc->dma_buffer_pool = gst_buffer_pool_new();

	pool_config = gst_buffer_pool_get_config(imx_vpu_enc->dma_buffer_pool);
	g_assert(pool_config != NULL);
	gst_buffer_pool_config_set_params(pool_config, NULL, imx_vpu_enc->current_stream_info.min_framebuffer_size, 0, 0);
	gst_buffer_pool_config_set_allocator(pool_config, imx_vpu_enc->default_dma_buf_allocator, &alloc_params);
	if (!gst_buffer_pool_set_config(imx_vpu_enc->dma_buffer_pool, pool_config))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not set DMA buffer pool configuration");
		goto error;
	}

	if (!gst_buffer_pool_set_active(imx_vpu_enc->dma_buffer_pool, TRUE))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not activate DMA buffer pool");
		goto error;
	}


finish:
	return ret;


error:
	if (imx_vpu_enc->dma_buffer_pool != NULL)
	{
		gst_object_unref(GST_OBJECT(imx_vpu_enc->dma_buffer_pool));
		imx_vpu_enc->dma_buffer_pool = NULL;
	}

	ret = FALSE;

	goto finish;
}


static void gst_imx_vpu_enc_free_fb_pool_dmabuffers(GstImxVpuEnc *imx_vpu_enc)
{
	if (imx_vpu_enc->fb_pool_buffers != NULL)
	{
		gst_buffer_list_unref(imx_vpu_enc->fb_pool_buffers);
		imx_vpu_enc->fb_pool_buffers = NULL;
	}
}


static GstFlowReturn gst_imx_vpu_enc_encode_queued_frames(GstImxVpuEnc *imx_vpu_enc)
{
	GstVideoEncoder *encoder = GST_VIDEO_ENCODER_CAST(imx_vpu_enc);
	GstFlowReturn flow_ret = GST_FLOW_OK;
	gboolean do_loop = TRUE;
	ImxVpuApiEncReturnCodes enc_ret;
	ImxVpuApiEncOutputCodes output_code;
	size_t encoded_frame_size;

	do_loop = TRUE;

	do
	{
		if (imx_vpu_enc->fatal_error_cannot_encode)
			break;

		GST_TRACE_OBJECT(imx_vpu_enc, "encoding");

		if ((enc_ret = imx_vpu_api_enc_encode(imx_vpu_enc->encoder, &encoded_frame_size, &output_code)) != IMX_VPU_API_ENC_RETURN_CODE_OK)
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "encoding frames failed: %s", imx_vpu_api_enc_return_code_string(enc_ret));
			flow_ret = GST_FLOW_ERROR;
			goto finish;
		}

		switch (output_code)
		{
			case IMX_VPU_API_ENC_OUTPUT_CODE_NEED_ADDITIONAL_FRAMEBUFFER:
			{
				GstBuffer *buffer = NULL;
				GstFlowReturn flow_ret;
				ImxDmaBuffer *fb_dma_buffer;

				GST_LOG_OBJECT(imx_vpu_enc, "need to acquire additional DMA buffer");

				flow_ret = gst_buffer_pool_acquire_buffer(imx_vpu_enc->dma_buffer_pool, &buffer, NULL);
				if (flow_ret != GST_FLOW_OK)
				{
					GST_ERROR_OBJECT(imx_vpu_enc, "could not acquire DMA buffer: %s", gst_flow_get_name(flow_ret));
					flow_ret = GST_FLOW_ERROR;
					goto finish;
				}

				gst_buffer_list_add(imx_vpu_enc->fb_pool_buffers, buffer);

				fb_dma_buffer = gst_imx_get_dma_buffer_from_buffer(buffer);
				if ((enc_ret = imx_vpu_api_enc_add_framebuffers_to_pool(imx_vpu_enc->encoder, &fb_dma_buffer, 1)) != IMX_VPU_API_ENC_RETURN_CODE_OK)
				{
					GST_ERROR_OBJECT(imx_vpu_enc, "could not add framebuffer to pool: %s", imx_vpu_api_enc_return_code_string(enc_ret));
					flow_ret = GST_FLOW_ERROR;
					goto finish;
				}

				break;
			}

			case IMX_VPU_API_ENC_OUTPUT_CODE_ENCODED_FRAME_AVAILABLE:
			{
				guint32 system_frame_number;
				GstMapInfo map_info;
				GstBuffer *output_buffer = NULL;
				ImxVpuApiEncodedFrame encoded_frame;
				GstVideoCodecFrame *out_frame;
				int is_sync_point = 0;

				if (encoded_frame_size > 0) {
					if ((output_buffer = gst_video_encoder_allocate_output_buffer(encoder, encoded_frame_size)) == NULL)
					{
						GST_ERROR_OBJECT(imx_vpu_enc, "could not allocate output buffer for encoded frame");
						flow_ret = GST_FLOW_ERROR;
						goto finish;
					}

					gst_buffer_map(output_buffer, &map_info, GST_MAP_WRITE);

					g_assert(map_info.size >= encoded_frame_size);
					memset(&encoded_frame, 0, sizeof(encoded_frame));
					encoded_frame.data = map_info.data;
					encoded_frame.data_size = encoded_frame_size;

					enc_ret = imx_vpu_api_enc_get_encoded_frame_ext(imx_vpu_enc->encoder, &encoded_frame, &is_sync_point);

					gst_buffer_unmap(output_buffer, &map_info);

					if (enc_ret != IMX_VPU_API_ENC_RETURN_CODE_OK)
					{
						GST_ERROR_OBJECT(imx_vpu_enc, "could not retrieve encoded frame: %s", imx_vpu_api_enc_return_code_string(enc_ret));
						flow_ret = GST_FLOW_ERROR;
						goto finish;
					}
				} else {
					flow_ret = GST_FLOW_OK;
					do_loop = FALSE;
				}

				if (encoded_frame.has_header && encoded_frame.header_size > 0 && imx_vpu_enc->cached_headers == NULL)
				{
					if (gst_buffer_map(output_buffer, &map_info, GST_MAP_READ))
					{
						imx_vpu_enc->cached_headers = g_malloc(encoded_frame.header_size);
						memcpy(imx_vpu_enc->cached_headers, map_info.data, encoded_frame.header_size);
						imx_vpu_enc->cached_headers_size = encoded_frame.header_size;
						gst_buffer_unmap(output_buffer, &map_info);
						GST_INFO_OBJECT(imx_vpu_enc, "cached %" G_GSIZE_FORMAT " bytes of parameter set headers", imx_vpu_enc->cached_headers_size);
					}
				}

				if (imx_vpu_enc->config_interval_frames > 0
				    && imx_vpu_enc->cached_headers != NULL
				    && !encoded_frame.has_header
				    && imx_vpu_enc->output_frame_count > 0
				    && (imx_vpu_enc->output_frame_count % imx_vpu_enc->config_interval_frames) == 0)
				{
					GstBuffer *header_buf = gst_buffer_new_wrapped(g_memdup2(imx_vpu_enc->cached_headers, imx_vpu_enc->cached_headers_size), imx_vpu_enc->cached_headers_size);
					output_buffer = gst_buffer_append(header_buf, output_buffer);
					GST_LOG_OBJECT(imx_vpu_enc, "prepended %" G_GSIZE_FORMAT " bytes of parameter sets at frame %" G_GUINT64_FORMAT,
						imx_vpu_enc->cached_headers_size, imx_vpu_enc->output_frame_count);
				}

				imx_vpu_enc->output_frame_count++;

				system_frame_number = (guint32)((guintptr)(encoded_frame.context));
				out_frame = gst_video_encoder_get_frame(encoder, system_frame_number);
				if (G_UNLIKELY(out_frame == NULL))
				{
					GST_WARNING_OBJECT(imx_vpu_enc, "no gstframe exists with number #%" G_GUINT32_FORMAT " - discarding encoded frame", system_frame_number);
					if (output_buffer)
						gst_buffer_unref(output_buffer);
					goto finish;
				}
				out_frame->output_buffer = output_buffer;

				if (is_sync_point)
					GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT(out_frame);

				flow_ret = gst_video_encoder_finish_frame(encoder, out_frame);

				g_hash_table_remove(imx_vpu_enc->uploaded_buffers_table, (gpointer)(gintptr)system_frame_number);

				break;
			}

			case IMX_VPU_API_ENC_OUTPUT_CODE_FRAME_SKIPPED:
			{
				guint32 system_frame_number;
				void *skipped_frame_context = NULL;
				uint64_t skipped_frame_pts = 0, skipped_frame_dts = 0;
				GstVideoCodecFrame *out_frame;

				enc_ret = imx_vpu_api_enc_get_skipped_frame_info(imx_vpu_enc->encoder, &skipped_frame_context, &skipped_frame_pts, &skipped_frame_dts);
				if (enc_ret != IMX_VPU_API_ENC_RETURN_CODE_OK)
				{
					GST_WARNING_OBJECT(imx_vpu_enc, "skipped-frame output without info available; ignoring");
					break;
				}

				system_frame_number = (guint32)((guintptr)skipped_frame_context);
				out_frame = gst_video_encoder_get_frame(encoder, system_frame_number);
				if (G_UNLIKELY(out_frame == NULL))
				{
					GST_WARNING_OBJECT(imx_vpu_enc, "no gstframe exists with number #%" G_GUINT32_FORMAT " - ignoring skipped frame", system_frame_number);
					break;
				}

				GST_INFO_OBJECT(imx_vpu_enc, "encoder skipped gstframe #%" G_GUINT32_FORMAT " (HW auto-recovery)", system_frame_number);

				out_frame->output_buffer = NULL;
				flow_ret = gst_video_encoder_finish_frame(encoder, out_frame);

				g_hash_table_remove(imx_vpu_enc->uploaded_buffers_table, (gpointer)(gintptr)system_frame_number);

				break;
			}

			case IMX_VPU_API_ENC_OUTPUT_CODE_MORE_INPUT_DATA_NEEDED:
				GST_LOG_OBJECT(imx_vpu_enc, "VPU has no more data to encode");
				do_loop = FALSE;
				break;

			case IMX_VPU_API_ENC_OUTPUT_CODE_EOS:
				GST_DEBUG_OBJECT(imx_vpu_enc, "VPU reports EOS; no more frames to encode");
				flow_ret = GST_FLOW_EOS;
				do_loop = FALSE;
				break;

			default:
				break;
		}
	}
	while (do_loop);


finish:
	if (flow_ret == GST_FLOW_ERROR)
		imx_vpu_enc->fatal_error_cannot_encode = TRUE;

	return flow_ret;
}


void gst_imx_vpu_enc_common_class_init(GstImxVpuEncClass *klass, ImxVpuApiCompressionFormat compression_format, gboolean with_rate_control, gboolean with_constant_quantization, gboolean with_gop_support, gboolean with_open_closed_gop_support, gboolean with_intra_refresh)
{
	GObjectClass *object_class;
	GstElementClass *element_class;
	GstPadTemplate *sink_template;
	GstPadTemplate *src_template;
	GstCaps *sink_template_caps;
	GstCaps *src_template_caps;
	gboolean got_caps;
	gchar *longname;
	gchar *classification;
	gchar *description;
	gchar *author;
	GstImxVpuCodecDetails const *codec_details;
	ImxVpuApiCompressionFormatSupportDetails const *format_support_details;

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	codec_details = gst_imx_vpu_get_codec_details(compression_format);
	format_support_details = imx_vpu_api_enc_get_compression_format_support_details(compression_format);

	g_type_set_qdata(G_OBJECT_CLASS_TYPE(klass), gst_imx_vpu_compression_format_quark(), (gpointer *)compression_format);

	got_caps = gst_imx_vpu_get_caps_for_format(compression_format, format_support_details, &src_template_caps, &sink_template_caps, TRUE);
	g_assert(got_caps);

	sink_template = gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_template_caps);
	src_template = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, src_template_caps);

	gst_element_class_add_pad_template(element_class, sink_template);
	gst_element_class_add_pad_template(element_class, src_template);

	klass->use_idr_frame_type_for_keyframes = FALSE;

	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_get_property);

	if (with_gop_support)
	{
		g_object_class_install_property(
			object_class,
			PROP_GOP_SIZE,
			g_param_spec_uint(
				"gop-size",
				"Group-of-picture size",
				"How many frames a group-of-picture shall contain",
				0, 32767,
				DEFAULT_GOP_SIZE,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
		if (with_open_closed_gop_support)
		{
			g_object_class_install_property(
				object_class,
				PROP_CLOSED_GOP_INTERVAL,
				g_param_spec_uint(
					"closed-gop-interval",
					"Closed GOP interval",
					"Interval between GOPs that are closed to previous GOPs; 0 = no closed GOPs",
					0, G_MAXUINT,
					DEFAULT_GOP_SIZE,
					G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
				)
			);
		}
	}
	if (with_rate_control)
	{
		g_object_class_install_property(
			object_class,
			PROP_BITRATE,
			g_param_spec_uint(
				"bitrate",
				"Bitrate",
				with_constant_quantization ? "Bitrate to use, in kbps (0 = no rate control; constant quality mode is used)" : "Bitrate to use, in kbps",
				with_constant_quantization ? 0 : 1, G_MAXUINT,
				DEFAULT_BITRATE,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
	}
	if (with_constant_quantization)
	{
		g_object_class_install_property(
			object_class,
			PROP_QUANTIZATION,
			g_param_spec_uint(
				"quantization",
				"Quantization",
				with_rate_control ? "Constant quantization factor to use if rate control is disabled (meaning, bitrate is set to 0)" : "Constant quantization factor to use",
				format_support_details->min_quantization, format_support_details->max_quantization,
				gst_imx_vpu_get_default_quantization(format_support_details),
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
	}
	if (with_intra_refresh)
	{
		g_object_class_install_property(
			object_class,
			PROP_INTRA_REFRESH,
			g_param_spec_boolean(
				"intra-refresh",
				"Intra refresh",
				"Keep the stream decodable by coding a band of CTB rows intra in every "
				"picture, sweeping the picture top to bottom, instead of by sending a "
				"periodic IDR. There is no IDR at all after the first one, so no "
				"periodic bitrate spike; a decoder joining mid-stream is complete one "
				"refresh period after the recovery point. Replaces use-intra-refresh, "
				"use-rolling-slices and use-rolling-tiles",
				DEFAULT_INTRA_REFRESH,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
		g_object_class_install_property(
			object_class,
			PROP_INTRA_REFRESH_PERIOD,
			g_param_spec_uint(
				"intra-refresh-period",
				"Intra refresh period",
				"How often a refresh sweep starts, in frames; 0 = use gop-size. This is "
				"decoupled from gop-size, which stays the rate control window. Shorter = "
				"faster mid-stream join, but a larger share of every picture is intra, so "
				"quality at a fixed bitrate falls: at 1400 kbps 720p30 on FPV footage a "
				"30 frame period measures 40.75 dB and an 11 frame one 39.08 dB",
				0, G_MAXUINT16, DEFAULT_INTRA_REFRESH_PERIOD,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
		g_object_class_install_property(
			object_class,
			PROP_INTRA_REFRESH_DURATION,
			g_param_spec_uint(
				"intra-refresh-duration",
				"Intra refresh duration",
				"How many frames one sweep is spread over; 0 = the whole period, which "
				"spreads the refresh evenly with no idle gap. A shorter duration bunches "
				"the same refreshes into fewer pictures: the picture is complete sooner "
				"after a recovery point, at the same average refresh cost, in exchange "
				"for frame size jitter (measured +31 ms p99 queueing delay with "
				"rate-control=1, +56 ms with the built-in one). Clamped down to the "
				"period. Needs rate-control=1: the encoder's own GDR spreads a sweep "
				"over the whole period and has no dial for this",
				0, 255, DEFAULT_INTRA_REFRESH_DURATION,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
		g_object_class_install_property(
			object_class,
			PROP_INTRA_REFRESH_ROWS,
			g_param_spec_uint(
				"intra-refresh-rows",
				"Intra refresh band height",
				"Target height of one refresh band, in the encoder's own coding unit "
				"rows: 64 pixels for h.265, 16 for h.264. A target, not an exact "
				"height - the sweep is split into ceil(rows-in-picture / this) bands of "
				"as equal a height as they divide into, so that they cover the picture "
				"exactly. 0 = a 128 pixel band, which is 2 rows on h.265 and 8 on "
				"h.264; that measures better than a single CTB row on both test clips "
				"and with either rate control. Needs rate-control=1: the encoder's own "
				"GDR derives the band height from the period",
				0, 255, DEFAULT_INTRA_REFRESH_ROWS,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
		g_object_class_install_property(
			object_class,
			PROP_SLICE_HEIGHT,
			g_param_spec_uint(
				"slice-height",
				"Slice height",
				"Slice height in the encoder's coding unit rows - 64 pixels for h.265, "
				"16 for h.264; 0 = one slice per picture. "
				"Slices are full width horizontal bands and nothing else - there is no "
				"byte or MTU based slicing on this encoder. They confine loss and give "
				"finer RTP fragmentation, and cost 0.19 dB on FPV footage and 0.70 dB on "
				"distant aerial footage at a fixed bitrate. Takes precedence over "
				"slice-count",
				0, 255, DEFAULT_SLICE_HEIGHT,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
		g_object_class_install_property(
			object_class,
			PROP_SLICE_COUNT,
			g_param_spec_uint(
				"slice-count",
				"Slice count",
				"Slices per picture, as an alternative to naming the height. Only the "
				"height is programmable - the hardware derives the count from it - so "
				"most counts do not exist and this rounds down to one that does, logging "
				"what it got: at 720p h.265, 12 CTB rows, only 1, 2, 3, 4, 6 and 12 are "
				"achievable, while h.264's 45 macroblock rows allow far more. "
				"0 or 1 = one slice",
				0, 255, DEFAULT_SLICE_COUNT,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
			)
		);
		g_object_class_install_property(
			object_class,
			PROP_USE_INTRA_REFRESH,
			g_param_spec_boolean(
				"use-intra-refresh",
				"Use intra refresh",
				"Use intra refresh instead of I/IDR frames and group-of-picture (GOP). "
				"DEPRECATED: use intra-refresh",
				DEFAULT_USE_INTRA_REFRESH,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_DEPRECATED
			)
		);
		g_object_class_install_property(
			object_class,
			PROP_USE_ROLLING_SLICES,
			g_param_spec_uint(
				"use-rolling-slices",
				"Rolling intra slice refresh",
				"Rolling intra refresh by full width slice band. 0 = off, 1 = automatic "
				"(4 slices), 2..16 = slice count. DEPRECATED: use intra-refresh with "
				"intra-refresh-rows and slice-count",
				0, 16, DEFAULT_USE_ROLLING_SLICES,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_DEPRECATED
			)
		);
		g_object_class_install_property(
			object_class,
			PROP_USE_ROLLING_TILES,
			g_param_spec_uint(
				"use-rolling-tiles",
				"Rolling intra tile refresh",
				"Rolling intra refresh by 2D tile, in a fixed 2 column by ceil(N/2) row "
				"grid. 0 = off, 1 = automatic (2x2), 2/4/.../16 = tile count. These are "
				"not HEVC tiles - the encoder core ignores its own tile registers - but "
				"rectangular refresh regions. Half width regions carry no refresh SEI, so "
				"a receiver cannot track recovery through them. DEPRECATED: use "
				"intra-refresh",
				0, 16, DEFAULT_USE_ROLLING_TILES,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_DEPRECATED
			)
		);
		g_object_class_install_property(
			object_class,
			PROP_ROLL_SIZE,
			g_param_spec_uint(
				"roll-size",
				"Rolling intra refresh sweep period",
				"Frames over which the rolling wave refreshes every region once; 0 = use "
				"gop-size, and it is clamped to gop-size. DEPRECATED: use "
				"intra-refresh-period, which is not clamped",
				0, G_MAXUINT16, DEFAULT_ROLL_SIZE,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_DEPRECATED
			)
		);
	}

	g_object_class_install_property(
		object_class,
		PROP_FIXED_INTRA_QUANTIZATION,
		g_param_spec_uint(
			"fixed-intra-quantization",
			"Fixed intra quantization",
			"Fixed quantization factor to use for intra frames; 0 = let rate control calculate quantization factors of intra frames instead; not used if bitrate is set to 0",
			0, format_support_details->max_quantization, DEFAULT_FIXED_INTRA_QUANTIZATION,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ALLOW_FRAMESKIPPING,
		g_param_spec_boolean(
			"allow-frameskipping",
			"Allow frameskipping",
			"Allow rate control to skip frames if necessary to maintain bitrate; not used if bitrate is set to 0",
			DEFAULT_ALLOW_FRAMESKIPPING,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	g_object_class_install_property(
		object_class,
		PROP_INTRA_QP_BIAS,
		g_param_spec_int(
			"intra-qp-bias",
			"Intra QP bias",
			"Bias for quantization factor to use for intra frames; this can be used to change the relative quality of the Intra pictures or to lower the size of Intra pictures",
			-12, 12, DEFAULT_INTRA_QP_BIAS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	g_object_class_install_property(
		object_class,
		PROP_HRD_BUFFER_SIZE,
		g_param_spec_uint(
			"hrd-buffer-size",
			"HRD CBP buffer size",
			"Size of Coded Picture Buffer in HRD (kbits)",
			0, G_MAXUINT, DEFAULT_HRD_BUFFER_SIZE,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	g_object_class_install_property(
		object_class,
		PROP_USE_HRD,
		g_param_spec_boolean(
			"use-hrd",
			"Use HRD",
			"Hypothetical Reference Decoder model, restricts the instantaneous bitrate and total bit amount of every coded picture; enabling HRD will cause tight constrains on the operation of the rate control",
			DEFAULT_USE_HRD,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);

	g_object_class_install_property(object_class, PROP_QP_MIN,
		g_param_spec_uint("qp-min", "Min QP (P/B)",
			"Minimum quantization parameter for P/B frames (quality ceiling); lower = sharper but bigger frames. 0 = let rate control decide",
			0, 51, DEFAULT_QP_MIN, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property(object_class, PROP_QP_MIN_INTRA,
		g_param_spec_uint("qp-min-intra", "Min QP (I)",
			"Minimum quantization parameter for I frames; lower = sharper keyframes but bigger. 0 = let rate control decide",
			0, 51, DEFAULT_QP_MIN_INTRA, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property(object_class, PROP_QP_MAX,
		g_param_spec_uint("qp-max", "Max QP (P/B)",
			"Maximum quantization parameter for P/B frames (quality floor); lower = better worst-case quality but bigger frames. 0 = let the codec decide (51, no ceiling)",
			0, 51, DEFAULT_QP_MAX, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property(object_class, PROP_QP_MAX_INTRA,
		g_param_spec_uint("qp-max-intra", "Max QP (I)",
			"Maximum quantization parameter for I frames; lower = better worst-case keyframe quality but bigger keyframes. 0 = let the codec decide (51, no ceiling)",
			0, 51, DEFAULT_QP_MAX_INTRA, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property(object_class, PROP_STATIC_SCENE_IBIT_PERCENT,
		g_param_spec_uint("static-scene-ibit-percent", "Static-scene intra bit %",
			"Extra bits (%) the encoder spends on intra content in detected static scenes; 0 = off (flat, no static-scene spike), higher = sharper static but bigger periodic refresh spike",
			0, 100, DEFAULT_STATIC_SCENE_IBIT_PERCENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property(object_class, PROP_GDR_REFRESH_PERIOD,
		g_param_spec_uint("gdr-refresh-period", "GDR refresh period",
			"Intra-refresh period in frames, decoupled from gop-size (which stays the rate-control window). 0 = use gop-size. DEPRECATED: use intra-refresh-period, which is the same thing without the 255 frame ceiling",
			0, 255, DEFAULT_GDR_REFRESH_PERIOD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_DEPRECATED));
	g_object_class_install_property(object_class, PROP_ROTATION,
		g_param_spec_uint("rotation", "Rotation",
			"Rotate the picture in the encoder pre-processor, in degrees. The PP rotates while reading the input frame (no extra memory pass, chroma-exact). Supported values: 0, 180",
			0, 180, DEFAULT_ROTATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property(object_class, PROP_RATE_CONTROL,
		g_param_spec_uint("rate-control", "Rate control",
			"Which rate control drives the encoder. 0 = the encoder's built-in one (default; use-hrd, static-scene-ibit-percent, qp-min etc. apply as documented). 1 = new CBR: the built-in picture rate control is switched off and every picture's QP is chosen from a leaky-bucket model instead (bounded by hrd-buffer-size); aims at a bitrate ceiling rather than a quota, so the rate follows scene difficulty without retuning. This also picks which mechanism produces the intra refresh, because the two cannot both drive it: 0 leaves it to the encoder's own GDR and produces byte-for-byte the stream the unmodified encoder produced, 1 runs the sweep from the plugin - which is what intra-refresh-duration, intra-refresh-rows and request-intra-region act on, and what emits the refresh SEIs. VC8000E only",
			0, 1, DEFAULT_RATE_CONTROL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	longname = g_strdup_printf("i.MX VPU %s video encoder", codec_details->desc_name);
	classification = g_strdup("Codec/Encoder/Video/Hardware");
	description = g_strdup_printf("Hardware-accelerated %s video encoding using the i.MX VPU codec", codec_details->desc_name);
	author = g_strdup("Carlos Rafael Giani <crg7475@mailbox.org>");
	gst_element_class_set_metadata(element_class, longname, classification, description, author);
	g_free(longname);
	g_free(classification);
	g_free(description);
	g_free(author);
}


void gst_imx_vpu_enc_common_init(GstImxVpuEnc *imx_vpu_enc)
{
	ImxVpuApiCompressionFormat compression_format = GST_IMX_VPU_GET_ELEMENT_COMPRESSION_FORMAT(imx_vpu_enc);

	imx_vpu_enc->quantization = gst_imx_vpu_get_default_quantization(imx_vpu_api_enc_get_compression_format_support_details(compression_format));
}
