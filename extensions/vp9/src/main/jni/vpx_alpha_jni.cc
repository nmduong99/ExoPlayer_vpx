/*
 * Copyright (C) 2025
 *
 * VP9 Alpha JNI Decoder
 * Style compatible with ExoPlayer vpx_jni.cc
 */

#include <android/log.h>
#include <jni.h>
#include <cstdlib>
#include <cstring>

#define VPX_CODEC_DISABLE_COMPAT 1
#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"
#include "ivf_demuxer.h"

#define LOG_TAG "www_vpx_alpha_jni"
#define LOGE(...) \
  ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

#define VPX_PLANE_ALPHA 3

// ===============================
// JNI MACRO (MATCH EXO STYLE)
// ===============================
#define ALPHA_DECODER_FUNC(RETURN_TYPE, NAME, ...)                  \
  extern "C" {                                                      \
  JNIEXPORT RETURN_TYPE                                             \
      Java_com_google_android_exoplayer2_ext_vp9_VpxAlphaDecoder_##NAME( \
          JNIEnv* env, jobject thiz, ##__VA_ARGS__);                \
  }                                                                 \
  JNIEXPORT RETURN_TYPE                                             \
      Java_com_google_android_exoplayer2_ext_vp9_VpxAlphaDecoder_##NAME( \
          JNIEnv* env, jobject thiz, ##__VA_ARGS__)

// ===============================
// NATIVE CONTEXT
// ===============================
struct AlphaJniCtx {
  vpx_codec_ctx_t decoder;
  vpx_image_t* image;
  IvfDemuxer demuxer;
  int initialized;
};

// ===============================
// JNI ONLOAD
// ===============================
jint JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return -1;
  }
  return JNI_VERSION_1_6;
}

// ===============================
// INIT
// ===============================
ALPHA_DECODER_FUNC(jlong, init, jint threads) {
  AlphaJniCtx* ctx = new AlphaJniCtx();
  memset(ctx, 0, sizeof(AlphaJniCtx));

  vpx_codec_dec_cfg_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.threads = threads > 0 ? threads : 2;

  vpx_codec_err_t err =
      vpx_codec_dec_init(&ctx->decoder,
                          &vpx_codec_vp9_dx_algo,
                          &cfg,
                          0);

  if (err != VPX_CODEC_OK) {
    LOGE("vpx_codec_dec_init failed: %d", err);
    delete ctx;
    return 0;
  }

  ctx->initialized = 1;
  ctx->image = nullptr;
  return reinterpret_cast<intptr_t>(ctx);
}


// ===============================
// INIT WITH FILE
// ===============================

ALPHA_DECODER_FUNC(jlong, initWithFile,
                   jstring jPath,
                   jint threads) {
  const char* path = env->GetStringUTFChars(jPath, nullptr);

  AlphaJniCtx* ctx = new AlphaJniCtx();
  memset(ctx, 0, sizeof(AlphaJniCtx));

  if (!ctx->demuxer.open(path)) {
    env->ReleaseStringUTFChars(jPath, path);
    delete ctx;
    return 0;
  }

  env->ReleaseStringUTFChars(jPath, path);

  vpx_codec_dec_cfg_t cfg = {0, 0, 0};
  cfg.threads = threads > 0 ? threads : 2;

  if (vpx_codec_dec_init(&ctx->decoder,
                          &vpx_codec_vp9_dx_algo,
                          &cfg, 0) != VPX_CODEC_OK) {
    delete ctx;
    return 0;
  }

  ctx->initialized = 1;
  return reinterpret_cast<intptr_t>(ctx);
}

// ===============================
// DECODE NEXT FRAME
// ===============================
ALPHA_DECODER_FUNC(jint, decodeNextFrame, jlong jContext) {
  AlphaJniCtx* ctx = reinterpret_cast<AlphaJniCtx*>(jContext);
  if (!ctx || !ctx->initialized) return -1;

  uint8_t* data = nullptr;
  size_t size = 0;

  if (!ctx->demuxer.readFrame(&data, &size)) {
    return 1; // EOF
  }

  if (vpx_codec_decode(&ctx->decoder, data, size, nullptr, 0) != VPX_CODEC_OK) {
    return -1;
  }

  vpx_codec_iter_t iter = nullptr;
  ctx->image = vpx_codec_get_frame(&ctx->decoder, &iter);

  return ctx->image ? 0 : 1;
}

// ===============================
// DECODE
// ===============================
ALPHA_DECODER_FUNC(jint, decode,
                   jlong jContext,
                   jobject encoded,
                   jint len) {
  AlphaJniCtx* ctx = reinterpret_cast<AlphaJniCtx*>(jContext);
  if (!ctx || !ctx->initialized) return -1;

  uint8_t* data =
      reinterpret_cast<uint8_t*>(env->GetDirectBufferAddress(encoded));
  if (!data) return -1;

  vpx_codec_err_t err =
      vpx_codec_decode(&ctx->decoder, data, len, nullptr, 0);

  if (err != VPX_CODEC_OK) {
    LOGE("vpx_codec_decode failed: %d", err);
    return -1;
  }

  vpx_codec_iter_t iter = nullptr;
  ctx->image = vpx_codec_get_frame(&ctx->decoder, &iter);

  return ctx->image ? 0 : 1;
}

// ===============================
// HAS FRAME
// ===============================
ALPHA_DECODER_FUNC(jboolean, hasFrame, jlong jContext) {
  AlphaJniCtx* ctx = reinterpret_cast<AlphaJniCtx*>(jContext);
  return (ctx && ctx->image) ? JNI_TRUE : JNI_FALSE;
}

// ===============================
// GET PLANE (Y / U / V / ALPHA)
// ===============================
ALPHA_DECODER_FUNC(jobject, getPlane,
                   jlong jContext,
                   jint plane) {
  AlphaJniCtx* ctx = reinterpret_cast<AlphaJniCtx*>(jContext);
  if (!ctx || !ctx->image) return nullptr;

  if (plane < 0 || plane > VPX_PLANE_ALPHA) return nullptr;

  uint8_t* ptr = ctx->image->planes[plane];
  if (!ptr) return nullptr;

  int stride = ctx->image->stride[plane];
  int height = ctx->image->d_h;

  return env->NewDirectByteBuffer(ptr, stride * height);
}

// ===============================
// GET STRIDE
// ===============================
ALPHA_DECODER_FUNC(jint, getStride,
                   jlong jContext,
                   jint plane) {
  AlphaJniCtx* ctx = reinterpret_cast<AlphaJniCtx*>(jContext);
  if (!ctx || !ctx->image) return 0;
  return ctx->image->stride[plane];
}

// ===============================
// GET WIDTH / HEIGHT
// ===============================
ALPHA_DECODER_FUNC(jint, getWidth, jlong jContext) {
  AlphaJniCtx* ctx = reinterpret_cast<AlphaJniCtx*>(jContext);
  return (ctx && ctx->image) ? ctx->image->d_w : 0;
}

ALPHA_DECODER_FUNC(jint, getHeight, jlong jContext) {
  AlphaJniCtx* ctx = reinterpret_cast<AlphaJniCtx*>(jContext);
  return (ctx && ctx->image) ? ctx->image->d_h : 0;
}

// ===============================
// RELEASE
// ===============================
ALPHA_DECODER_FUNC(void, close, jlong jContext) {
  AlphaJniCtx* ctx = reinterpret_cast<AlphaJniCtx*>(jContext);
  if (!ctx) return;

  if (ctx->initialized) {
    vpx_codec_destroy(&ctx->decoder);
  }
  ctx->demuxer.close();
  delete ctx;
}
