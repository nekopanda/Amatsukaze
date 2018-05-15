#pragma once

#include <avisynth.h>

struct AMTGenTime {
  enum
  {
    VERSION = 1,
    MAGIC_KEY = 0x6080EDF8,
  };
  int nMagicKey;
  int nVersion;

  AMTGenTime()
    : nMagicKey(MAGIC_KEY)
    , nVersion(VERSION)
  { }

  static const AMTGenTime* GetParam(const VideoInfo& vi)
  {
    if (vi.sample_type != MAGIC_KEY) {
      return nullptr;
    }
    const AMTGenTime* param = (const AMTGenTime*)(void*)vi.num_audio_samples;
    if (param->nMagicKey != MAGIC_KEY) {
      return nullptr;
    }
    return param;
  }

  static void SetParam(VideoInfo& vi, const AMTGenTime* param)
  {
    vi.audio_samples_per_second = 0; // kill audio
    vi.sample_type = MAGIC_KEY;
    vi.num_audio_samples = (size_t)param;
  }
};

enum AMT_FRAME_FPS {
  AMT_FPS_24 = 1,
  AMT_FPS_30 = 2,
  AMT_FPS_60 = 3,
};

struct AMTFrameFps {
  int fps;
  int source;
};
