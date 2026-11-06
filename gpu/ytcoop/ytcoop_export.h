// SPDX-License-Identifier: GPL-2.0-or-later
// BloodOnline (Yharnam Together module): included first in every source of the module. The co-op
// code's C interface (bbcoop.h) is what the runtime and the renderer's overlay call (dlsym), so it
// keeps default visibility while everything else in the module is hidden.
#pragma once
#ifdef __cplusplus
#include "ytcoop_compat.h"
#endif
#pragma GCC visibility push(default)
#include "bbcoop.h"
#pragma GCC visibility pop
