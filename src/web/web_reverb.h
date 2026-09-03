#pragma once

// Audio-device boundary only. Input is four planar ACN/N3D wet channels;
// output is planar stereo. Channel stride is 1024 floats; process 1..1024
// frames at a time. Buffers remain valid until Initialize or Shutdown.
// This DSP runs separately from the game Worker and owns no SND/alias state.
extern "C" {
int WebReverb_Initialize(unsigned sampleRate);
int WebReverb_SetRoom(int room);
float *WebReverb_Input();
float *WebReverb_Output();
int WebReverb_Process(unsigned frames);
void WebReverb_Shutdown();
}
