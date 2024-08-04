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
#include "gstimxvpucommon.h"
#include "gstimxvpuenc.h"
#include "gstimxvpuench265.h"


enum
{
	PROP_0 = GST_IMX_VPU_ENC_BASE_PROP_VALUE,
	PROP_ENABLE_AUD
};


#define DEFAULT_ENABLE_AUD TRUE


GST_DEBUG_CATEGORY_STATIC(imx_vpu_enc_h265_debug);
#define GST_CAT_DEFAULT imx_vpu_enc_h265_debug


struct _GstImxVpuEncH265
{
	GstImxVpuEnc parent;

	gboolean enable_aud;
};


struct _GstImxVpuEncH265Class
{
	GstImxVpuEncClass parent;
};


G_DEFINE_TYPE(GstImxVpuEncH265, gst_imx_vpu_enc_h265, GST_TYPE_IMX_VPU_ENC)


static void gst_imx_vpu_enc_h265_set_encoder_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_vpu_enc_h265_get_encoder_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static gboolean gst_imx_vpu_enc_h265_set_open_params(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncOpenParams *open_params);
static GstCaps* gst_imx_vpu_enc_h265_get_output_caps(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info);


static void gst_imx_vpu_enc_h265_class_init(GstImxVpuEncH265Class *klass)
{
	GObjectClass *object_class;
	GstImxVpuEncClass *imx_vpu_enc_class;

	GST_DEBUG_CATEGORY_INIT(imx_vpu_enc_h265_debug, "imxvpuenc_h265", 0, "NXP i.MX VPU h.265 video encoder");

	object_class = G_OBJECT_CLASS(klass);
	imx_vpu_enc_class = GST_IMX_VPU_ENC_CLASS(klass);

	imx_vpu_enc_class->set_encoder_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h265_set_encoder_property);
	imx_vpu_enc_class->get_encoder_property = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h265_get_encoder_property);
	imx_vpu_enc_class->set_open_params = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h265_set_open_params);
	imx_vpu_enc_class->get_output_caps = GST_DEBUG_FUNCPTR(gst_imx_vpu_enc_h265_get_output_caps);

	gst_imx_vpu_enc_common_class_init(imx_vpu_enc_class, IMX_VPU_API_COMPRESSION_FORMAT_H265, TRUE, TRUE, TRUE, TRUE, TRUE);

	imx_vpu_enc_class->use_idr_frame_type_for_keyframes = TRUE;

	g_object_class_install_property(
		object_class,
		PROP_ENABLE_AUD,
		g_param_spec_boolean(
			"enable-aud",
			"Enable access unit delimiters",
			"Enable the generation of access unit delimiters in the encoded output",
			DEFAULT_ENABLE_AUD,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_vpu_enc_h265_init(GstImxVpuEncH265 *imx_vpu_enc_h265)
{
	gst_imx_vpu_enc_common_init(GST_IMX_VPU_ENC_CAST(imx_vpu_enc_h265));
	imx_vpu_enc_h265->enable_aud = DEFAULT_ENABLE_AUD;
}


static void gst_imx_vpu_enc_h265_set_encoder_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxVpuEncH265 *imx_vpu_enc_h265 = GST_IMX_VPU_ENC_H265(object);

	switch (prop_id)
	{
		case PROP_ENABLE_AUD:
			GST_OBJECT_LOCK(imx_vpu_enc_h265);
			imx_vpu_enc_h265->enable_aud = g_value_get_boolean(value);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h265);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}


static void gst_imx_vpu_enc_h265_get_encoder_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxVpuEncH265 *imx_vpu_enc_h265 = GST_IMX_VPU_ENC_H265(object);

	switch (prop_id)
	{
		case PROP_ENABLE_AUD:
			GST_OBJECT_LOCK(imx_vpu_enc_h265);
			g_value_set_boolean(value, imx_vpu_enc_h265->enable_aud);
			GST_OBJECT_UNLOCK(imx_vpu_enc_h265);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}


gboolean gst_imx_vpu_enc_h265_set_open_params(GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncOpenParams *open_params)
{
	gboolean ret = TRUE;
	GstStructure *s;
	gchar const *str;
	GstCaps *allowed_srccaps;
	ImxVpuApiEncH265OpenParams *h265_params = &(open_params->format_specific_open_params.h265_open_params);

	allowed_srccaps = gst_pad_get_allowed_caps(GST_VIDEO_DECODER_SRC_PAD(imx_vpu_enc));

	if (allowed_srccaps == NULL)
		allowed_srccaps = gst_pad_get_pad_template_caps(GST_VIDEO_DECODER_SRC_PAD(imx_vpu_enc));
	if (G_UNLIKELY(allowed_srccaps == NULL))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not set h.265 params; unable to get allowed src caps or src template caps");
		ret = FALSE;
		goto finish;
	}
	else if (G_UNLIKELY(gst_caps_is_empty(allowed_srccaps)))
	{
		GST_ERROR_OBJECT(imx_vpu_enc, "could not set h.265 params; downstream caps are empty");
		ret = FALSE;
		goto finish;
	}

	s = gst_caps_get_structure(allowed_srccaps, 0);

	str = gst_imx_vpu_get_string_from_structure_field(s, "profile");
	if (str != NULL)
	{
		if (g_strcmp0(str, "main") == 0)                 h265_params->profile = IMX_VPU_API_H265_PROFILE_MAIN;
		else if (g_strcmp0(str, "main-10") == 0)                 h265_params->profile = IMX_VPU_API_H265_PROFILE_MAIN10;
		else
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "unsupported h.265 profile \"%s\"", str);
			ret = FALSE;
			goto finish;
		}
	}

	str = gst_imx_vpu_get_string_from_structure_field(s, "level");
	if (str != NULL)
	{
		if      (g_strcmp0(str, "1") == 0)   h265_params->level = IMX_VPU_API_H265_LEVEL_1;
		else if (g_strcmp0(str, "2") == 0)   h265_params->level = IMX_VPU_API_H265_LEVEL_2;
		else if (g_strcmp0(str, "2.1") == 0) h265_params->level = IMX_VPU_API_H265_LEVEL_2_1;
		else if (g_strcmp0(str, "3") == 0)   h265_params->level = IMX_VPU_API_H265_LEVEL_3;
		else if (g_strcmp0(str, "3.1") == 0) h265_params->level = IMX_VPU_API_H265_LEVEL_3_1;
		else if (g_strcmp0(str, "4") == 0)   h265_params->level = IMX_VPU_API_H265_LEVEL_4;
		else if (g_strcmp0(str, "4.1") == 0) h265_params->level = IMX_VPU_API_H265_LEVEL_4_1;
		else if (g_strcmp0(str, "5") == 0)   h265_params->level = IMX_VPU_API_H265_LEVEL_5;
		else if (g_strcmp0(str, "5.1") == 0) h265_params->level = IMX_VPU_API_H265_LEVEL_5_1;
		else if (g_strcmp0(str, "5.2") == 0) h265_params->level = IMX_VPU_API_H265_LEVEL_5_2;
		else if (g_strcmp0(str, "6") == 0)   h265_params->level = IMX_VPU_API_H265_LEVEL_6;
		else if (g_strcmp0(str, "6.1") == 0) h265_params->level = IMX_VPU_API_H265_LEVEL_6_1;
		else if (g_strcmp0(str, "6.2") == 0) h265_params->level = IMX_VPU_API_H265_LEVEL_6_2;
		else
		{
			GST_ERROR_OBJECT(imx_vpu_enc, "unsupported h.265 level \"%s\"", str);
			ret = FALSE;
			goto finish;
		}
	}

	GST_OBJECT_LOCK(imx_vpu_enc);
	h265_params->enable_access_unit_delimiters = GST_IMX_VPU_ENC_H265_CAST(imx_vpu_enc)->enable_aud;
	GST_OBJECT_UNLOCK(imx_vpu_enc);

	GST_INFO_OBJECT(imx_vpu_enc, "access unit delimiters enabled: %d", h265_params->enable_access_unit_delimiters);


finish:
	if (allowed_srccaps != NULL)
		gst_caps_unref(allowed_srccaps);

	return ret;
}


GstCaps* gst_imx_vpu_enc_h265_get_output_caps(G_GNUC_UNUSED GstImxVpuEnc *imx_vpu_enc, ImxVpuApiEncStreamInfo const *stream_info)
{
	GstCaps *caps;
	gchar const *level_str, *profile_str;

	switch (stream_info->format_specific_open_params.h265_open_params.level)
	{
		case IMX_VPU_API_H265_LEVEL_1:   level_str = "1";   break;
		case IMX_VPU_API_H265_LEVEL_2:   level_str = "2";   break;
		case IMX_VPU_API_H265_LEVEL_2_1: level_str = "2.1"; break;
		case IMX_VPU_API_H265_LEVEL_3:   level_str = "3";   break;
		case IMX_VPU_API_H265_LEVEL_3_1: level_str = "3.1"; break;
		case IMX_VPU_API_H265_LEVEL_4:   level_str = "4";   break;
		case IMX_VPU_API_H265_LEVEL_4_1: level_str = "4.1"; break;
		case IMX_VPU_API_H265_LEVEL_5:   level_str = "5";   break;
		case IMX_VPU_API_H265_LEVEL_5_1: level_str = "5.1"; break;
		case IMX_VPU_API_H265_LEVEL_5_2: level_str = "5.2"; break;
		case IMX_VPU_API_H265_LEVEL_6:   level_str = "6";   break;
		case IMX_VPU_API_H265_LEVEL_6_1: level_str = "6.1"; break;
		case IMX_VPU_API_H265_LEVEL_6_2: level_str = "6.2"; break;
		default: g_assert_not_reached();
	}

	switch (stream_info->format_specific_open_params.h265_open_params.profile)
	{
		case IMX_VPU_API_H265_PROFILE_MAIN:                 profile_str = "main";                 break;
		case IMX_VPU_API_H265_PROFILE_MAIN10:               profile_str = "main-10";              break;
		default: g_assert_not_reached();
	}

	caps = gst_caps_new_simple(
		"video/x-h265",
		"stream-format", G_TYPE_STRING,     "byte-stream",
		/* "au" refers to "access unit". This is unrelated to access unit delimiters,
		 * and instead means that the encoder always produces entire access unit,
		 * which the CODA VPU does. */
		"alignment",     G_TYPE_STRING,     "au",
		"level",         G_TYPE_STRING,     level_str,
		"profile",       G_TYPE_STRING,     profile_str,
		"width",         G_TYPE_INT,        (gint)(stream_info->frame_encoding_framebuffer_metrics.actual_frame_width),
		"height",        G_TYPE_INT,        (gint)(stream_info->frame_encoding_framebuffer_metrics.actual_frame_height),
		"framerate",     GST_TYPE_FRACTION, (gint)(stream_info->frame_rate_numerator), (gint)(stream_info->frame_rate_denominator),
		NULL
	);

	return caps;
}
