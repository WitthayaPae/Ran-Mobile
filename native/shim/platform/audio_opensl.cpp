//  Android's half of the audio: an OpenSL ES buffer queue that asks the mixer
//  for frames. Nothing else in the tree knows OpenSL exists.
//
//  OpenSL ES rather than AAudio because AAudio is API 26 and this build targets
//  24 - the same reason the image decoder resolves AImageDecoder at runtime.
//  The mixing is ours either way, so the sink is the only part that would
//  change.
#ifdef __ANDROID__

#include "audio_mix.h"
#include "ran_plat.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <string.h>
#include <vector>

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanAudio", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanAudio", __VA_ARGS__)

namespace {

//  Two buffers of 20 ms. Short enough that a sound starts when it is asked for,
//  long enough that a busy frame does not starve the queue: at 44.1 kHz that is
//  882 frames a buffer, and the callback has a whole buffer of slack.
const int kFramesPerBuffer = 882;
const int kBuffers = 2;

SLObjectItf g_engineObj = NULL;
SLEngineItf g_engine    = NULL;
SLObjectItf g_mixObj    = NULL;
SLObjectItf g_playerObj = NULL;
SLPlayItf   g_play      = NULL;
SLAndroidSimpleBufferQueueItf g_queue = NULL;

std::vector<short> g_buf[kBuffers];
int g_next = 0;
bool g_running = false;

void queueCallback ( SLAndroidSimpleBufferQueueItf bq, void * )
{
    if (!g_running) return;
    std::vector<short> &b = g_buf[g_next];
    g_next = ( g_next + 1 ) % kBuffers;
    RanAudio_Mix ( &b[0], kFramesPerBuffer );
    (*bq)->Enqueue ( bq, &b[0], (SLuint32) ( b.size() * sizeof(short) ) );
}

} // namespace

extern "C" int RanAudioSink_Start ( void )
{
    if (g_running) return 1;

    if (slCreateEngine ( &g_engineObj, 0, NULL, 0, NULL, NULL ) != SL_RESULT_SUCCESS) {
        LOGE ( "slCreateEngine failed" );
        return 0;
    }
    if ((*g_engineObj)->Realize ( g_engineObj, SL_BOOLEAN_FALSE ) != SL_RESULT_SUCCESS ||
        (*g_engineObj)->GetInterface ( g_engineObj, SL_IID_ENGINE, &g_engine ) != SL_RESULT_SUCCESS) {
        LOGE ( "engine realize failed" );
        return 0;
    }
    if ((*g_engine)->CreateOutputMix ( g_engine, &g_mixObj, 0, NULL, NULL ) != SL_RESULT_SUCCESS ||
        (*g_mixObj)->Realize ( g_mixObj, SL_BOOLEAN_FALSE ) != SL_RESULT_SUCCESS) {
        LOGE ( "output mix failed" );
        return 0;
    }

    SLDataLocator_AndroidSimpleBufferQueue loc = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, (SLuint32) kBuffers };
    SLDataFormat_PCM fmt;
    fmt.formatType   = SL_DATAFORMAT_PCM;
    fmt.numChannels  = RANAUDIO_CHANNELS;
    fmt.samplesPerSec = RANAUDIO_RATE * 1000;      //  milliHertz, despite the name
    fmt.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
    fmt.containerSize = 16;
    fmt.channelMask  = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    fmt.endianness   = SL_BYTEORDER_LITTLEENDIAN;

    SLDataSource src = { &loc, &fmt };
    SLDataLocator_OutputMix outLoc = { SL_DATALOCATOR_OUTPUTMIX, g_mixObj };
    SLDataSink sink = { &outLoc, NULL };

    const SLInterfaceID ids[] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
    const SLboolean req[] = { SL_BOOLEAN_TRUE };
    if ((*g_engine)->CreateAudioPlayer ( g_engine, &g_playerObj, &src, &sink,
                                         1, ids, req ) != SL_RESULT_SUCCESS ||
        (*g_playerObj)->Realize ( g_playerObj, SL_BOOLEAN_FALSE ) != SL_RESULT_SUCCESS ||
        (*g_playerObj)->GetInterface ( g_playerObj, SL_IID_PLAY, &g_play ) != SL_RESULT_SUCCESS ||
        (*g_playerObj)->GetInterface ( g_playerObj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                       &g_queue ) != SL_RESULT_SUCCESS) {
        LOGE ( "audio player failed" );
        return 0;
    }

    for (int i = 0; i < kBuffers; ++i)
        g_buf[i].assign ( (size_t) kFramesPerBuffer * RANAUDIO_CHANNELS, 0 );

    (*g_queue)->RegisterCallback ( g_queue, queueCallback, NULL );
    g_running = true;
    (*g_play)->SetPlayState ( g_play, SL_PLAYSTATE_PLAYING );

    //  Prime every buffer: the callback only fires after one has been played,
    //  so an unprimed queue never starts.
    for (int i = 0; i < kBuffers; ++i) queueCallback ( g_queue, NULL );

    //  Everything enqueued is audio the device has not played yet.
    RanAudio_SetSinkLatency ( kFramesPerBuffer * kBuffers );

    LOGI ( "OpenSL ES out: %d Hz stereo, %d x %d frames",
           RANAUDIO_RATE, kBuffers, kFramesPerBuffer );
    return 1;
}

//  The app went to the background. Pausing the player stops the buffer-queue
//  callback, so the mixer is not run at all - a muted mixer would still cost a
//  wakeup 50 times a second for nothing.
extern "C" void RanAudioSink_Pause ( int paused )
{
    if (!g_play) return;
    (*g_play)->SetPlayState ( g_play, paused ? SL_PLAYSTATE_PAUSED : SL_PLAYSTATE_PLAYING );
    LOGI ( "audio %s", paused ? "paused" : "resumed" );
}

extern "C" void RanAudioSink_Stop ( void )
{
    g_running = false;
    if (g_play) (*g_play)->SetPlayState ( g_play, SL_PLAYSTATE_STOPPED );
    if (g_playerObj) { (*g_playerObj)->Destroy ( g_playerObj ); g_playerObj = NULL; }
    if (g_mixObj)    { (*g_mixObj)->Destroy ( g_mixObj );       g_mixObj = NULL; }
    if (g_engineObj) { (*g_engineObj)->Destroy ( g_engineObj ); g_engineObj = NULL; }
    g_play = NULL; g_queue = NULL; g_engine = NULL;
}

#endif  //  __ANDROID__
