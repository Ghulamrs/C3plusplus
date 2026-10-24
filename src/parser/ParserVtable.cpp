// The parser: the Itanium ABI's vtable group, thunks and VTT. A `_ZTV` holds
// the primary table, one per non-primary base at any depth and one per
// virtual base with a vptr.

// A class with virtual bases also gets construction vtables and a VTT its
// base-subobject constructor reads its vptrs from.

// Measured against clang's -fdump-vtable-layouts and its `_ZTT` and `_ZTC`
// symbols; TMS6747.md, "Virtual inheritance".

#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

// ----- what a constructor does with all that
