/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/xaudio2/xaudio2_audio_driver.h"

#include "xenia/apu/apu-private.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/emulator.h"

namespace xe {
namespace apu {
namespace xaudio2 {

class XAudio2AudioDriver::VoiceCallback : public IXAudio2VoiceCallback {
 public:
  VoiceCallback(HANDLE wait_handle) : wait_handle_(wait_handle) {}
  ~VoiceCallback() {}

  void OnStreamEnd() {}
  void OnVoiceProcessingPassEnd() {}
  void OnVoiceProcessingPassStart(uint32_t samples_required) {}
  void OnBufferEnd(void* context) { SetEvent(wait_handle_); }
  void OnBufferStart(void* context) {}
  void OnLoopEnd(void* context) {}
  void OnVoiceError(void* context, HRESULT result) {}

 private:
  HANDLE wait_handle_;
};

XAudio2AudioDriver::XAudio2AudioDriver(Emulator* emulator, HANDLE wait)
    : audio_(nullptr),
      mastering_voice_(nullptr),
      pcm_voice_(nullptr),
      wait_handle_(wait),
      voice_callback_(nullptr),
      current_frame_(0),
      AudioDriver(emulator) {}

XAudio2AudioDriver::~XAudio2AudioDriver() = default;

const DWORD ChannelMasks[] = {
    0,  // TODO: fixme
    0,  // TODO: fixme
    SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_LOW_FREQUENCY,
    0,  // TODO: fixme
    0,  // TODO: fixme
    0,  // TODO: fixme
    SPEAKER_FRONT_LEFT | SPEAKER_FRONT_CENTER | SPEAKER_FRONT_RIGHT |
        SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT,
    0,  // TODO: fixme
};

void XAudio2AudioDriver::Initialize() {
  HRESULT hr;

  voice_callback_ = new VoiceCallback(wait_handle_);

  hr = XAudio2Create(&audio_, 0, XAUDIO2_DEFAULT_PROCESSOR);
  if (FAILED(hr)) {
    XELOGE("XAudio2Create failed with %.8X", hr);
    assert_always();
    return;
  }

  XAUDIO2_DEBUG_CONFIGURATION config;
  config.TraceMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS;
  config.BreakMask = 0;
  config.LogThreadID = FALSE;
  config.LogTiming = TRUE;
  config.LogFunctionName = TRUE;
  config.LogFileline = TRUE;
  audio_->SetDebugConfiguration(&config);

  hr = audio_->CreateMasteringVoice(&mastering_voice_);
  if (FAILED(hr)) {
    XELOGE("CreateMasteringVoice failed with %.8X", hr);
    assert_always();
    return;
  }

  WAVEFORMATIEEEFLOATEX waveformat;

  waveformat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  waveformat.Format.nChannels = frame_channels_;
  waveformat.Format.nSamplesPerSec = 48000;
  waveformat.Format.wBitsPerSample = 32;
  waveformat.Format.nBlockAlign =
      (waveformat.Format.nChannels * waveformat.Format.wBitsPerSample) / 8;
  waveformat.Format.nAvgBytesPerSec =
      waveformat.Format.nSamplesPerSec * waveformat.Format.nBlockAlign;
  waveformat.Format.cbSize =
      sizeof(WAVEFORMATIEEEFLOATEX) - sizeof(WAVEFORMATEX);

  waveformat.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  waveformat.Samples.wValidBitsPerSample = waveformat.Format.wBitsPerSample;
  waveformat.dwChannelMask = ChannelMasks[waveformat.Format.nChannels];

  hr = audio_->CreateSourceVoice(
      &pcm_voice_, &waveformat.Format,
      0,  // XAUDIO2_VOICE_NOSRC | XAUDIO2_VOICE_NOPITCH,
      XAUDIO2_MAX_FREQ_RATIO, voice_callback_);
  if (FAILED(hr)) {
    XELOGE("CreateSourceVoice failed with %.8X", hr);
    assert_always();
    return;
  }

  hr = pcm_voice_->Start();
  if (FAILED(hr)) {
    XELOGE("Start failed with %.8X", hr);
    assert_always();
    return;
  }

  if (FLAGS_mute) {
    pcm_voice_->SetVolume(0.0f);
  }

  SetEvent(wait_handle_);
}

void XAudio2AudioDriver::SubmitFrame(uint32_t frame_ptr) {
  // Process samples! They are big-endian floats.
  HRESULT hr;

  XAUDIO2_VOICE_STATE state;
  pcm_voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
  assert_true(state.BuffersQueued < frame_count_);

  auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);
  auto output_frame = reinterpret_cast<float*>(frames_[current_frame_]);
  auto interleave_channels = frame_channels_;

  // interleave the data
  for (uint32_t index = 0, o = 0; index < channel_samples_; ++index) {
    for (uint32_t channel = 0, table = 0; channel < interleave_channels;
         ++channel, table += channel_samples_) {
      output_frame[o++] = xe::byte_swap(input_frame[table + index]);
    }
  }

  XAUDIO2_BUFFER buffer;
  buffer.Flags = 0;
  buffer.pAudioData = (BYTE*)output_frame;
  buffer.AudioBytes = frame_size_;
  buffer.PlayBegin = 0;
  buffer.PlayLength = channel_samples_;
  buffer.LoopBegin = XAUDIO2_NO_LOOP_REGION;
  buffer.LoopLength = 0;
  buffer.LoopCount = 0;
  buffer.pContext = 0;
  hr = pcm_voice_->SubmitSourceBuffer(&buffer);
  if (FAILED(hr)) {
    XELOGE("SubmitSourceBuffer failed with %.8X", hr);
    assert_always();
    return;
  }

  current_frame_ = (current_frame_ + 1) % frame_count_;

  // Update playback ratio to our time scalar.
  // This will keep audio in sync with the game clock.
  pcm_voice_->SetFrequencyRatio(float(xe::Clock::guest_time_scalar()));

  XAUDIO2_VOICE_STATE state2;
  pcm_voice_->GetState(&state2, XAUDIO2_VOICE_NOSAMPLESPLAYED);

  if (state2.BuffersQueued >= frame_count_) {
    ResetEvent(wait_handle_);
  }
}

void XAudio2AudioDriver::Shutdown() {
  pcm_voice_->Stop();
  pcm_voice_->DestroyVoice();
  pcm_voice_ = NULL;

  mastering_voice_->DestroyVoice();
  mastering_voice_ = NULL;

  audio_->StopEngine();
  audio_->Release();

  delete voice_callback_;
}

}  // namespace xaudio2
}  // namespace apu
}  // namespace xe
