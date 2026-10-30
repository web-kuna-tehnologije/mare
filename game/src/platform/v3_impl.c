// The v3 framework as one C translation unit (docs/REWRITE_PLAN.md): the whole engine
// implementation compiles here and nowhere else. The C++ game links against it through
// src/platform/v3cpp.hh (declarations with C linkage). This file is the last C in the build.
#define V3_IMPLEMENTATION
#include "v3.h"
