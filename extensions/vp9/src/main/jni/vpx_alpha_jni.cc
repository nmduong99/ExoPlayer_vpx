/*
 * Copyright (C) 2025
 *
 * VP9 Alpha JNI Decoder (IVF + libvpx)
/*
 * VP9 Dual-Track (Color+Alpha) JNI Decoder using libvpx + mkvparser demuxer
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

#define ALPHA_DECODER_FUNC(RETURN_TYPE, NAME, ...)                  \
  extern "C" {                                                      \
  JNIEXPORT RETURN_TYPE                                             \
      Java_com_dw_vp9_decoder_NativeVp9Decoder_##NAME(              \
          JNIEnv* env, jobject thiz, ##__VA_ARGS__);                \
  }                                                                 \
  JNIEXPORT RETURN_TYPE                                             \
      Java_com_dw_vp9_decoder_NativeVp9Decoder_##NAME(              \
          JNIEnv* env, jobject thiz, ##__VA_ARGS__)

struct AlphaJniCtx {
  vpx_codec_ctx_t decoder_color;
  vpx_codec_ctx_t decoder_alpha;
  vpx_image_t* image_color;
  vpx_image_t* image_alpha;
  WebmDemuxer demuxer;
  bool initialized;
};

static AlphaJniCtx* getCtx(JNIEnv* env, jobject thiz) {
  jclass cls = env->GetObjectClass(thiz);
  jfieldID fid = env->GetFieldID(cls, "nativeHandle", "J");
  return reinterpret_cast<AlphaJniCtx*>(env->GetLongField(thiz, fid));
}

static void setCtx(JNIEnv* env, jobject thiz, AlphaJniCtx* ctx) {
  jclass cls = env->GetObjectClass(thiz);
  jfieldID fid = env->GetFieldID(cls, "nativeHandle", "J");
  env->SetLongField(thiz, fid, reinterpret_cast<jlong>(ctx));
}

jint JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return -1;
  }
  return JNI_VERSION_1_6;
}

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
    LOGE("vpx_codec_dec_init(color) failed: %s", vpx_codec_error(&ctx->decoder_color));
    delete ctx;
    return JNI_FALSE;
  }

  if (vpx_codec_dec_init(&ctx->decoder_alpha, &vpx_codec_vp9_dx_algo, &cfg, 0) != VPX_CODEC_OK) {
    LOGE("vpx_codec_dec_init(alpha) failed: %s", vpx_codec_error(&ctx->decoder_alpha));
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

ALPHA_DECODER_FUNC(jboolean, decodeNextFrame) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx || !ctx->initialized) return JNI_FALSE;

  const uint8_t* cdata = nullptr;
  const uint8_t* adata = nullptr;
  size_t csize = 0;
  size_t asize = 0;
  int64_t tns = -1;

  if (!ctx->demuxer.readPairedFrame(&cdata, &csize, &adata, &asize, &tns)) {
    return JNI_FALSE;
  }

  if (vpx_codec_decode(&ctx->decoder_color, cdata, (unsigned int)csize, nullptr, 0) != VPX_CODEC_OK) {
    LOGE("vpx_codec_decode(color) failed: %s", vpx_codec_error(&ctx->decoder_color));
    return JNI_FALSE;
  }

  if (vpx_codec_decode(&ctx->decoder_alpha, adata, (unsigned int)asize, nullptr, 0) != VPX_CODEC_OK) {
    LOGE("vpx_codec_decode(alpha) failed: %s", vpx_codec_error(&ctx->decoder_alpha));
    return JNI_FALSE;
  }

  vpx_codec_iter_t itc = nullptr;
  vpx_codec_iter_t ita = nullptr;
  ctx->image_color = vpx_codec_get_frame(&ctx->decoder_color, &itc);
  ctx->image_alpha = vpx_codec_get_frame(&ctx->decoder_alpha, &ita);

  if (!ctx->image_color || !ctx->image_alpha) return JNI_FALSE;

  // Debug: alpha stream should have Y plane
  // LOGE("fmtC=%d Y=%p U=%p V=%p | fmtA=%d AY=%p",
  //      (int)ctx->image_color->fmt,
  //      ctx->image_color->planes[0], ctx->image_color->planes[1], ctx->image_color->planes[2],
  //      (int)ctx->image_alpha->fmt,
  //      ctx->image_alpha->planes[0]);

  return JNI_TRUE;
}

ALPHA_DECODER_FUNC(jint, getWidth) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image_color) ? ctx->image_color->d_w : 0;
}

ALPHA_DECODER_FUNC(jint, getHeight) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image_color) ? ctx->image_color->d_h : 0;
}

static jobject makePlaneBuffer(JNIEnv* env, const vpx_image_t* img, int plane, int height) {
  if (!img) return nullptr;
  if (!img->planes[plane]) return nullptr;
  return env->NewDirectByteBuffer(img->planes[plane], img->stride[plane] * height);
}

ALPHA_DECODER_FUNC(jobject, getYPlane) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return makePlaneBuffer(env, ctx ? ctx->image_color : nullptr, VPX_PLANE_Y,
                         ctx && ctx->image_color ? ctx->image_color->d_h : 0);
}

ALPHA_DECODER_FUNC(jobject, getUPlane) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx || !ctx->image_color) return nullptr;
  int uvHeight = (ctx->image_color->d_h + 1) / 2;
  return makePlaneBuffer(env, ctx->image_color, VPX_PLANE_U, uvHeight);
}

ALPHA_DECODER_FUNC(jobject, getVPlane) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx || !ctx->image_color) return nullptr;
  int uvHeight = (ctx->image_color->d_h + 1) / 2;
  return makePlaneBuffer(env, ctx->image_color, VPX_PLANE_V, uvHeight);
}

ALPHA_DECODER_FUNC(jobject, getAlphaPlane) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx || !ctx->image_alpha) return nullptr;
  // alpha track is grayscale -> use Y plane
  return makePlaneBuffer(env, ctx->image_alpha, VPX_PLANE_Y, ctx->image_alpha->d_h);
}

ALPHA_DECODER_FUNC(jint, getYStride) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image_color) ? ctx->image_color->stride[VPX_PLANE_Y] : 0;
}

ALPHA_DECODER_FUNC(jint, getUStride) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image_color) ? ctx->image_color->stride[VPX_PLANE_U] : 0;
}

ALPHA_DECODER_FUNC(jint, getVStride) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image_color) ? ctx->image_color->stride[VPX_PLANE_V] : 0;
}

ALPHA_DECODER_FUNC(jint, getAlphaStride) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image_alpha) ? ctx->image_alpha->stride[VPX_PLANE_Y] : 0;
}

ALPHA_DECODER_FUNC(jboolean, seekTo, jlong timeMs) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx || !ctx->initialized) return JNI_FALSE;

  // Minimal: demuxer seek not implemented in sample code.
  // If you implement demuxer.seekMs, you should also reset both decoders.
  // return ctx->demuxer.seekMs(timeMs) ? JNI_TRUE : JNI_FALSE;

  (void)timeMs;
  return JNI_FALSE;
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