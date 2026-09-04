//  iOS's half of the audio: an AudioQueue that asks the mixer for frames.
//  The exact counterpart of audio_opensl.cpp, and the only iOS-specific part of
//  the sound system - everything above it is portable.
//
//  AudioQueue rather than AVAudioEngine or a RemoteIO audio unit: the mixing is
//  already ours, so all that is wanted is a callback that hands over PCM, and
//  AudioQueue is the smallest API that does exactly that. It has been there
//  since iOS 2 and is not deprecated.
//
//  NOT YET COMPILED - there is no Mac on this machine.
#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

#include "audio_mix.h"
#include "ran_plat.h"

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanAudio", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanAudio", __VA_ARGS__)

namespace {

//  The same 20 ms as Android, for the same reason: short enough that a sound
//  starts when it is asked for, long enough that a busy frame does not starve
//  the queue. Three buffers rather than two because AudioQueue enqueues ahead.
const int kFramesPerBuffer = 882;
const int kBuffers = 3;

AudioQueueRef       g_queue = NULL;
AudioQueueBufferRef g_buf[kBuffers];
bool g_running = false;

void queueCallback ( void *, AudioQueueRef q, AudioQueueBufferRef buf )
{
    const UInt32 bytes = (UInt32) ( kFramesPerBuffer * RANAUDIO_CHANNELS * sizeof(short) );
    if (!g_running) {
        memset ( buf->mAudioData, 0, bytes );
    } else {
        RanAudio_Mix ( (short *) buf->mAudioData, kFramesPerBuffer );
    }
    buf->mAudioDataByteSize = bytes;
    AudioQueueEnqueueBuffer ( q, buf, 0, NULL );
}

} // namespace

extern "C" int RanAudioSink_Start ( void )
{
    if (g_running) return 1;

    //  Ambient, and not mixable-with-others by default: this is a game, it owns
    //  the output while it is in front. The category also decides what the
    //  silent switch does - ambient respects it, which is what a player
    //  expects from a game they muted with the hardware switch.
    NSError *err = nil;
    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:&err];
    if (err) LOGE ( "audio session category: %s", err.localizedDescription.UTF8String );
    [[AVAudioSession sharedInstance] setActive:YES error:&err];
    if (err) LOGE ( "audio session activate: %s", err.localizedDescription.UTF8String );

    AudioStreamBasicDescription fmt;
    memset ( &fmt, 0, sizeof(fmt) );
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    fmt.mSampleRate       = RANAUDIO_RATE;
    fmt.mChannelsPerFrame = RANAUDIO_CHANNELS;
    fmt.mBitsPerChannel   = 16;
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerFrame    = RANAUDIO_CHANNELS * sizeof(short);
    fmt.mBytesPerPacket   = fmt.mBytesPerFrame;

    OSStatus st = AudioQueueNewOutput ( &fmt, queueCallback, NULL, NULL, NULL, 0, &g_queue );
    if (st != noErr) { LOGE ( "AudioQueueNewOutput failed: %d", (int) st ); return 0; }

    const UInt32 bytes = (UInt32) ( kFramesPerBuffer * RANAUDIO_CHANNELS * sizeof(short) );
    for (int i = 0; i < kBuffers; ++i) {
        st = AudioQueueAllocateBuffer ( g_queue, bytes, &g_buf[i] );
        if (st != noErr) { LOGE ( "AudioQueueAllocateBuffer failed: %d", (int) st ); return 0; }
    }

    g_running = true;

    //  Primed with silence and enqueued before start: the callback only fires
    //  for a buffer that has been played, so an empty queue never starts.
    for (int i = 0; i < kBuffers; ++i) {
        memset ( g_buf[i]->mAudioData, 0, bytes );
        g_buf[i]->mAudioDataByteSize = bytes;
        AudioQueueEnqueueBuffer ( g_queue, g_buf[i], 0, NULL );
    }

    st = AudioQueueStart ( g_queue, NULL );
    if (st != noErr) { LOGE ( "AudioQueueStart failed: %d", (int) st ); g_running = false; return 0; }

    LOGI ( "AudioQueue out: %d Hz stereo, %d x %d frames",
           RANAUDIO_RATE, kBuffers, kFramesPerBuffer );
    return 1;
}

//  The app went to the background. Pausing stops the callback, so the mixer is
//  not run at all - the same trade as the Android sink.
extern "C" void RanAudioSink_Pause ( int paused )
{
    if (!g_queue) return;
    if (paused) AudioQueuePause ( g_queue );
    else        AudioQueueStart ( g_queue, NULL );
    LOGI ( "audio %s", paused ? "paused" : "resumed" );
}

extern "C" void RanAudioSink_Stop ( void )
{
    g_running = false;
    if (g_queue) {
        AudioQueueStop ( g_queue, true );
        AudioQueueDispose ( g_queue, true );
        g_queue = NULL;
    }
    [[AVAudioSession sharedInstance] setActive:NO error:nil];
}

#endif  //  __APPLE__
