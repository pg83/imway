#include "color.h"

bool directScanoutColorCompatible(bool outputHdr, bool surfaceColorManaged) {
    return !outputHdr && !surfaceColorManaged;
}
