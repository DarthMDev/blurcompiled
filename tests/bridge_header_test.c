#include "render/plume_bridge.h"
_Static_assert(sizeof(BlurPlumeError) == 512, "C error layout");
_Static_assert(sizeof(BlurPlumeSubmission) == 40, "C submission layout");
_Static_assert(sizeof(BlurPlumeOutput) == 176, "C output layout");
_Static_assert(sizeof(BlurPlumeStats) == 40, "C stats layout");
_Static_assert(sizeof(BlurPlumeApi) == 56, "C API table layout");
_Static_assert(sizeof(BlurPlumeCompositeSubmission) == 64, "C composite submission layout");
_Static_assert(sizeof(BlurPlumeCompositeOutput) == 176, "C composite output layout");
_Static_assert(sizeof(BlurPlumeApiV2) == 72, "C API V2 table layout");
/* Compiled as C to keep the host interface independent of C++ headers/ABI. */
