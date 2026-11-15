#pragma once

// The C6000 optimizer over one function's emitted text: the -O0 code is
// serial and padded to every latency, so with its NOPs removed it reads as a
// sequential program, and the pass re-pads it to the hazards that remain.

#include "C6xMir.h"

#include <string>

// The packets of `before`, a selected body's entries, `after`. `declined` names why the serial text is returned
// unscheduled (R108); `refused` names an instruction no unit encodes, which the caller refuses.
std::string c6xSchedule(const std::string &before, const c6x::MStream *selected, const std::string &after, int level,
                        std::string &declined, std::string &refused);
