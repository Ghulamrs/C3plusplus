#pragma once

// The C6000 optimizer over one function's emitted text: the -O0 code is
// serial and padded to every latency, so with its NOPs removed it reads as a
// sequential program, and the pass re-pads it to the hazards that remain.

#include "C6xMir.h"

#include <string>

// The packets made on the function's stream - `before`, a selected body's entries where there is one, `after`;
// `spelled` false: a line it does not spell, the text returned to be refused.
std::string c6xSchedule(const std::string &before, const c6x::MStream *selected, const std::string &after, int level, bool &spelled);
