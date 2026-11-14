#pragma once

// The C6000 optimizer over one function's emitted text: the -O0 code is
// serial and padded to every latency, so with its NOPs removed it reads as a
// sequential program, and the pass re-pads it to the hazards that remain.

#include <string>

// Every padding NOP replaced by the wait the pending writes require; a selected function's packets are made on its MIR.
std::string c6xSchedule(const std::string &text, int level, bool selected = false);
