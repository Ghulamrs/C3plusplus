#pragma once

// **The language a translation unit is read as** (design 3.2, M7): `.c` is C90 unless -std= says otherwise.
enum class Dialect { C90, Cxx11 };
