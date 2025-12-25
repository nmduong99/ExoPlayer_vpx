/*
 * Copyright (C) 2025
 *
 * VP9 Alpha JNI Decoder (IVF + libvpx)
 * Macro style compatible with ExoPlayer vpx_jni.cc
 */

#include <jni.h>
#include <android/log.h>
#include <cstdlib>
#include <cstring>

#define VPX_CODEC_DISABLE_COMPAT 1
#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"
#include "webm_demuxer.h"

#define LOG_TAG "www_vpx_alpha_jni"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define VPX_PLANE_Y 0
#define VPX_PLANE_U 1
#define VPX_PLANE_V 2
#define VPX_PLANE_ALPHA 3

// ======================================================
// JNI MACRO (MATCH EXO STYLE)
// ======================================================
#define ALPHA_DECODER_FUNC(RETURN_TYPE, NAME, ...)                  \
  extern "C" {                                                      \
  JNIEXPORT RETURN_TYPE                                             \
      Java_com_dw_vp9_decoder_NativeVp9Decoder_##NAME(              \
          JNIEnv* env, jobject thiz, ##__VA_ARGS__);                \
  }                                                                 \
  JNIEXPORT RETURN_TYPE                                             \
      Java_com_dw_vp9_decoder_NativeVp9Decoder_##NAME(              \
          JNIEnv* env, jobject thiz, ##__VA_ARGS__)

// ======================================================
// NATIVE CONTEXT
// ======================================================
struct AlphaJniCtx {
  vpx_codec_ctx_t decoder_color;
  vpx_codec_ctx_t decoder_alpha;
  vpx_image_t* image_color;
  vpx_image_t* image_alpha;
  WebmDemuxer demuxer;
  bool initialized;
};

// init: initialize BOTH decoders
ALPHA_DECODER_FUNC(jboolean, init, jstring jPath) {
  const char* path = env->GetStringUTFChars(jPath, nullptr);

  AlphaJniCtx* ctx = new AlphaJniCtx();
  std::memset(ctx, 0, sizeof(AlphaJniCtx));

  if (!ctx->demuxer.open(path)) {
    LOGE("demuxer.open failed: %s", path);
    env->ReleaseStringUTFChars(jPath, path);
    delete ctx;
    return JNI_FALSE;
  }
  env->ReleaseStringUTFChars(jPath, path);

  vpx_codec_dec_cfg_t cfg{};
  cfg.threads = 2;

  if (vpx_codec_dec_init(&ctx->decoder_color, &vpx_codec_vp9_dx_algo, &cfg, 0) != VPX_CODEC_OK) {
    LOGE("vpx_codec_dec_init (color) failed");
    delete ctx;
    return JNI_FALSE;
  }

  if (vpx_codec_dec_init(&ctx->decoder_alpha, &vpx_codec_vp9_dx_algo, &cfg, 0) != VPX_CODEC_OK) {
    LOGE("vpx_codec_dec_init (alpha) failed");
    vpx_codec_destroy(&ctx->decoder_color);
    delete ctx;
    return JNI_FALSE;
  }

  ctx->initialized = true;
  ctx->image_color = nullptr;
  ctx->image_alpha = nullptr;

  setCtx(env, thiz, ctx);
  return JNI_TRUE;
}

// decodeNextFrame: read paired packets and decode both
ALPHA_DECODER_FUNC(jboolean, decodeNextFrame) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx || !ctx->initialized) return JNI_FALSE;

  const uint8_t* cdata = nullptr;
  const uint8_t* adata = nullptr;
  size_t csize = 0, asize = 0;
  int64_t tns = -1;

  if (!ctx->demuxer.readPairedFrame(&cdata, &csize, &adata, &asize, &tns)) {
    return JNI_FALSE; // EOF or error
  }

  if (vpx_codec_decode(&ctx->decoder_color, cdata, (unsigned int)csize, nullptr, 0) != VPX_CODEC_OK) {
    LOGE("vpx_codec_decode(color) failed: %s", vpx_codec_error(&ctx->decoder_color));
    return JNI_FALSE;
  }

  if (vpx_codec_decode(&ctx->decoder_alpha, adata, (unsigned int)asize, nullptr, 0) != VPX_CODEC_OK) {
    LOGE("vpx_codec_decode(alpha) failed: %s", vpx_codec_error(&ctx->decoder_alpha));
    return JNI_FALSE;
  }

  vpx_codec_iter_t iter_c = nullptr;
  vpx_codec_iter_t iter_a = nullptr;
  ctx->image_color = vpx_codec_get_frame(&ctx->decoder_color, &iter_c);
  ctx->image_alpha = vpx_codec_get_frame(&ctx->decoder_alpha, &iter_a);

  return (ctx->image_color && ctx->image_alpha) ? JNI_TRUE : JNI_FALSE;
}

ALPHA_DECODER_FUNC(jint, getWidth) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image_color) ? ctx->image_color->d_w : 0;
}

ALPHA_DECODER_FUNC(jint, getHeight) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image_color) ? ctx->image_color->d_h : 0;
}

// Y from color
ALPHA_DECODER_FUNC(jobject, getYPlane) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx || !ctx->image_color) return nullptr;
  return env->NewDirectByteBuffer(ctx->image_color->planes[VPX_PLANE_Y],
                                  ctx->image_color->stride[VPX_PLANE_Y] * ctx->image_color->d_h);
}

// UV from color (return U plane like your old API; if you need V too, add another method)
ALPHA_DECODER_FUNC(jobject, getUVPlane) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx || !ctx->image_color) return nullptr;
  int uvHeight = (ctx->image_color->d_h + 1) / 2;
  return env->NewDirectByteBuffer(ctx->image_color->planes[VPX_PLANE_U],
                                  ctx->image_color->stride[VPX_PLANE_U] * uvHeight);
}

// Alpha: use Y plane from alpha stream (grayscale)
ALPHA_DECODER_FUNC(jobject, getAlphaPlane) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx || !ctx->image_alpha) return nullptr;
  return env->NewDirectByteBuffer(ctx->image_alpha->planes[VPX_PLANE_Y],
                                  ctx->image_alpha->stride[VPX_PLANE_Y] * ctx->image_alpha->d_h);
}

ALPHA_DECODER_FUNC(jint, getAlphaStride) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image_alpha) ? ctx->image_alpha->stride[VPX_PLANE_Y] : 0;
}

ALPHA_DECODER_FUNC(void, release) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx) return;

  if (ctx->initialized) {
    vpx_codec_destroy(&ctx->decoder_color);
    vpx_codec_destroy(&ctx->decoder_alpha);
  }
  ctx->demuxer.close();
  delete ctx;
  setCtx(env, thiz, nullptr);
}