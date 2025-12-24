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
#include "ivf_demuxer.h"

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
  vpx_codec_ctx_t decoder;
  vpx_image_t* image;
  IvfDemuxer demuxer;
  bool initialized;
};

// ======================================================
// nativeHandle helpers
// ======================================================
static AlphaJniCtx* getCtx(JNIEnv* env, jobject thiz) {
  jclass cls = env->GetObjectClass(thiz);
  jfieldID fid = env->GetFieldID(cls, "nativeHandle", "J");
  return reinterpret_cast<AlphaJniCtx*>(
      env->GetLongField(thiz, fid));
}

static void setCtx(JNIEnv* env, jobject thiz, AlphaJniCtx* ctx) {
  jclass cls = env->GetObjectClass(thiz);
  jfieldID fid = env->GetFieldID(cls, "nativeHandle", "J");
  env->SetLongField(thiz, fid, reinterpret_cast<jlong>(ctx));
}

// ======================================================
// JNI_OnLoad
// ======================================================
jint JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return -1;
  }
  return JNI_VERSION_1_6;
}

// ======================================================
// init(path: String): Boolean
// ======================================================
ALPHA_DECODER_FUNC(jboolean, init, jstring jPath) {
  const char* path = env->GetStringUTFChars(jPath, nullptr);

  AlphaJniCtx* ctx = new AlphaJniCtx();
  std::memset(ctx, 0, sizeof(AlphaJniCtx));

  if (!ctx->demuxer.open(path)) {
    LOGE("Failed to open IVF file");
    env->ReleaseStringUTFChars(jPath, path);
    delete ctx;
    return JNI_FALSE;
  }
  env->ReleaseStringUTFChars(jPath, path);

  vpx_codec_dec_cfg_t cfg{};
  cfg.threads = 2;

  if (vpx_codec_dec_init(
          &ctx->decoder,
          &vpx_codec_vp9_dx_algo,
          &cfg, 0) != VPX_CODEC_OK) {
    LOGE("vpx_codec_dec_init failed");
    delete ctx;
    return JNI_FALSE;
  }

  ctx->initialized = true;
  ctx->image = nullptr;
  setCtx(env, thiz, ctx);
  return JNI_TRUE;
}

// ======================================================
// decodeNextFrame(): Boolean
// ======================================================
ALPHA_DECODER_FUNC(jboolean, decodeNextFrame) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx || !ctx->initialized) return JNI_FALSE;

  uint8_t* data = nullptr;
  size_t size = 0;

  if (!ctx->demuxer.readFrame(&data, &size)) {
    return JNI_FALSE; // EOF
  }

  if (vpx_codec_decode(&ctx->decoder, data, size, nullptr, 0) != VPX_CODEC_OK) {
    LOGE("vpx_codec_decode failed");
    return JNI_FALSE;
  }

  vpx_codec_iter_t iter = nullptr;
  ctx->image = vpx_codec_get_frame(&ctx->decoder, &iter);
  return ctx->image != nullptr ? JNI_TRUE : JNI_FALSE;
}

// ======================================================
// getWidth / getHeight
// ======================================================
ALPHA_DECODER_FUNC(jint, getWidth) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image) ? ctx->image->d_w : 0;
}

ALPHA_DECODER_FUNC(jint, getHeight) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image) ? ctx->image->d_h : 0;
}

// ======================================================
// Plane helpers
// ======================================================
static jobject planeBuffer(JNIEnv* env,
                           AlphaJniCtx* ctx,
                           int plane,
                           int height) {
  if (!ctx || !ctx->image) return nullptr;
  if (!ctx->image->planes[plane]) return nullptr;

  return env->NewDirectByteBuffer(
      ctx->image->planes[plane],
      ctx->image->stride[plane] * height);
}

// ======================================================
// getYPlane / getUVPlane / getAlphaPlane
// ======================================================
ALPHA_DECODER_FUNC(jobject, getYPlane) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return planeBuffer(env, ctx, VPX_PLANE_Y, ctx->image->d_h);
}

ALPHA_DECODER_FUNC(jobject, getUVPlane) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  int uvHeight = (ctx->image->d_h + 1) / 2;
  return planeBuffer(env, ctx, VPX_PLANE_U, uvHeight);
}

ALPHA_DECODER_FUNC(jobject, getAlphaPlane) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return planeBuffer(env, ctx, VPX_PLANE_ALPHA, ctx->image->d_h);
}

// ======================================================
// getYStride / getUVStride / getAlphaStride
// ======================================================
ALPHA_DECODER_FUNC(jint, getYStride) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image) ? ctx->image->stride[VPX_PLANE_Y] : 0;
}

ALPHA_DECODER_FUNC(jint, getUVStride) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image) ? ctx->image->stride[VPX_PLANE_U] : 0;
}

ALPHA_DECODER_FUNC(jint, getAlphaStride) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  return (ctx && ctx->image) ? ctx->image->stride[VPX_PLANE_ALPHA] : 0;
}

// ======================================================
// release()
// ======================================================
ALPHA_DECODER_FUNC(void, release) {
  AlphaJniCtx* ctx = getCtx(env, thiz);
  if (!ctx) return;

  if (ctx->initialized) {
    vpx_codec_destroy(&ctx->decoder);
  }
  ctx->demuxer.close();
  delete ctx;
  setCtx(env, thiz, nullptr);
}
