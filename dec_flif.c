/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / FLIF decoder filter, based on the FLIF
 *  reference implementation (https://github.com/FLIF-hub/FLIF), linked in its
 *  decoder-only form (libflif_dec.a).
 *
 *  FLIF files can hold an animation; only the first frame is output here.
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <string.h>
#include <stdlib.h>

#include <flif/flif_dec.h>

typedef struct
{
	GF_FilterPid *ipid, *opid;
	Bool is_playing;
} GF_FLIFDecCtx;

static GF_Err flifdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	GF_FLIFDecCtx *ctx = (GF_FLIFDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	ctx->ipid = pid;
	gf_filter_pid_set_framing_mode(pid, GF_TRUE);

	if (!ctx->opid)
		ctx->opid = gf_filter_pid_new(filter);

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_VISUAL));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	return GF_OK;
}

static Bool flifdec_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_FLIFDecCtx *ctx = (GF_FLIFDecCtx *)gf_filter_get_udta(filter);
	switch (evt->base.type)
	{
	case GF_FEVT_PLAY:
		ctx->is_playing = GF_TRUE;
		return GF_FALSE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		return GF_FALSE;
	}
}

static GF_Err flifdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output, *row = NULL;
	u32 size, out_size, width, height, y, x, nb_comp;
	FLIF_DECODER *dec;
	FLIF_IMAGE *image;
	GF_FLIFDecCtx *ctx = (GF_FLIFDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);
	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	dec = flif_create_decoder();
	if (!dec)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}
	if (!flif_decoder_decode_memory(dec, data, size) || !flif_decoder_num_images(dec))
	{
		flif_destroy_decoder(dec);
		gf_filter_pid_drop_packet(ctx->ipid);
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[FLIFDec] Failed to decode FLIF image\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}
	gf_filter_pid_drop_packet(ctx->ipid);

	image = flif_decoder_get_image(dec, 0);
	if (!image)
	{
		flif_destroy_decoder(dec);
		return GF_NON_COMPLIANT_BITSTREAM;
	}
	width = flif_image_get_width(image);
	height = flif_image_get_height(image);
	nb_comp = (flif_image_get_nb_channels(image) >= 4) ? 4 : 3;

	out_size = width * height * nb_comp;

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT(width));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT(height));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STRIDE, &PROP_UINT(width * nb_comp));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT((nb_comp == 4) ? GF_PIXEL_RGBA : GF_PIXEL_RGB));

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_size, &output);
	if (!dst_pck)
	{
		flif_destroy_decoder(dec);
		return GF_OUT_OF_MEM;
	}

	/* The library only exposes rows as RGBA8, so an RGB output drops the
	 * alpha column while copying. */
	row = (u8 *)gf_malloc((size_t)width * 4);
	if (!row)
	{
		flif_destroy_decoder(dec);
		return GF_OUT_OF_MEM;
	}
	for (y = 0; y < height; y++)
	{
		flif_image_read_row_RGBA8(image, y, row, (size_t)width * 4);
		if (nb_comp == 4)
		{
			memcpy(output + (size_t)y * width * 4, row, (size_t)width * 4);
		}
		else
		{
			for (x = 0; x < width; x++)
			{
				u8 *dst = output + ((size_t)y * width + x) * 3;
				dst[0] = row[x * 4];
				dst[1] = row[x * 4 + 1];
				dst[2] = row[x * 4 + 2];
			}
		}
	}
	gf_free(row);
	flif_destroy_decoder(dec);

	gf_filter_pck_set_cts(dst_pck, 0);
	gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_set_eos(ctx->opid);
	return GF_EOS;
}

static void flifdec_finalize(GF_Filter *filter)
{
}

static const GF_FilterCapability FLIFDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "flif"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "image/flif"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister FLIFDecoderRegister = {
	.name = "flifdec",
	GF_FS_SET_DESCRIPTION("FLIF image decoder")
		GF_FS_SET_HELP("This filter decodes FLIF (Free Lossless Image Format) images using the FLIF reference decoder.")
			.private_size = sizeof(GF_FLIFDecCtx),
	SETCAPS(FLIFDecCaps),
	.configure_pid = flifdec_configure_pid,
	.process = flifdec_process,
	.process_event = flifdec_process_event,
	.finalize = flifdec_finalize,
};

const GF_FilterRegister *EMSCRIPTEN_KEEPALIVE flifdec_register(GF_FilterSession *session)
{
	return &FLIFDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_flifdec(void) {
    gf_filter_auto_register("flifdec", flifdec_register);
}
