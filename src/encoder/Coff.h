#pragma once

#include "AsmUnit.h"

namespace masm {

class CoffWriter : public ObjWriter {
public:
    bool write(const Unit &u, const std::string &path, std::string &err);
};

}
