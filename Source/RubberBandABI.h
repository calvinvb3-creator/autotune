#pragma once
// Production builds use the vendor's actual header. The small ABI declaration
// below permits testing against the system runtime when development headers
// are unavailable; it is not a replacement implementation of Rubber Band.
#ifdef PW_VENDOR_HEADER
#include <rubberband/rubberband-c.h>
#else
extern "C" {
struct RubberBandState_;
typedef RubberBandState_* RubberBandState;
typedef int RubberBandOptions;
RubberBandState rubberband_new(unsigned int, unsigned int, RubberBandOptions, double, double);
void rubberband_delete(RubberBandState);
void rubberband_reset(RubberBandState);
void rubberband_set_pitch_scale(RubberBandState, double);
void rubberband_set_max_process_size(RubberBandState, unsigned int);
unsigned int rubberband_get_preferred_start_pad(RubberBandState);
unsigned int rubberband_get_start_delay(RubberBandState);
void rubberband_process(RubberBandState, const float* const*, unsigned int, int);
int rubberband_available(RubberBandState);
unsigned int rubberband_retrieve(RubberBandState, float* const*, unsigned int);
}
#endif
