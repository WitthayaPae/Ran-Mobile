//  The mixer. Portable: no Android, no iOS, no DirectSound.
//
//  Everything above it speaks DirectSound (dsutil_mobile.cpp and the
//  IDirectSoundBuffer objects beside it); everything below is one platform sink
//  that asks for frames - OpenSL ES on Android, AudioQueue on iOS. This is the
//  only place that knows how a voice becomes samples.
//
//  Output is 16-bit stereo at a fixed rate. The client's content is 44.1 kHz
//  (832 sfx WAVs and 31 OGG streams), so resampling is only ever a small
//  correction, done per voice with a linear step.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RANAUDIO_RATE      44100
#define RANAUDIO_CHANNELS  2

//  Starts the mixer. The sink calls RanAudio_Mix from its own thread.
int  RanAudio_Init ( void );
void RanAudio_Shutdown ( void );

//  ------------------------------------------------------------------- clips
//
//  A clip is decoded PCM held once and shared by every voice playing it: the
//  same footstep is played from a dozen places and must not be decoded a dozen
//  times.

//  Decodes a RIFF/WAVE file (PCM or IEEE float) into a clip. Returns 0 on
//  failure, which the caller should treat as "no sound", never as an error.
int  RanAudio_LoadWav ( const char *path );
int  RanAudio_LoadWavMemory ( const void *data, size_t size );
void RanAudio_ReleaseClip ( int clip );

//  Frames in a clip, for callers that ask how long a sound is.
int  RanAudio_ClipFrames ( int clip );

//  ------------------------------------------------------------------ voices
//
//  A voice is one playing instance. DirectSound's units are kept as they are -
//  hundredths of a decibel for volume and pan - so the call sites read the same
//  as the PC ones and nothing has to be converted twice.

int  RanAudio_VoiceCreate ( int clip );
void RanAudio_VoiceDestroy ( int voice );
void RanAudio_VoicePlay ( int voice, int loop );
void RanAudio_VoiceStop ( int voice );
int  RanAudio_VoiceIsPlaying ( int voice );
void RanAudio_VoiceSetVolume ( int voice, long centibels );   //  -10000..0
void RanAudio_VoiceSetPan ( int voice, long centibels );      //  -10000..10000
void RanAudio_VoiceSetFrequency ( int voice, unsigned hz );   //  0 = the clip's own
void RanAudio_VoiceSetPosition ( int voice, unsigned byteOffset );
unsigned RanAudio_VoicePosition ( int voice );                //  byte offset

//  ------------------------------------------------------------ ring voices
//
//  What a streaming DirectSound buffer is: the client writes PCM into a ring
//  through Lock/Unlock and the mixer reads it. The music path uses this - the
//  engine decodes OGG itself and hands over the samples.

int  RanAudio_RingCreate ( unsigned bytes, int channels, int bitsPerSample, unsigned rate );
void *RanAudio_RingData ( int voice );          //  the buffer to write into
unsigned RanAudio_RingBytes ( int voice );

//  --------------------------------------------------------------- the sink
//
//  Called from the audio thread. Fills interleaved stereo 16-bit frames and
//  never blocks: a mixer that waits for a lock held by the game thread is a
//  glitch a player can hear.
void RanAudio_Mix ( short *out, int frames );

//  Master volume, applied after every voice. The options screen sets sfx and
//  music separately, and both arrive as DirectSound centibels on the voices, so
//  this is only the mute the platform asks for when the app loses focus.
void RanAudio_SetMuted ( int muted );

//  What the run actually played, for the shutdown line.
void RanAudio_LogStats ( void );

#ifdef __cplusplus
}
#endif
