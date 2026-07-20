#include "composer.h"

#include "input_router.h"

using namespace stl;

Composer::Composer(ObjPool* p)
    : pool(p)
{
    entry = createInputRouter(*this);
}
