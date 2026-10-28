// Metal builds compile sokol's implementations as Objective-C++ (with -fobjc-arc): the Metal
// backend and sokol_app's Cocoa windowing are Objective-C. Same content as sokol.cpp, which
// picks SOKOL_METAL from GAMEENGINE_METAL. Named differently so the two TUs don't share an
// object file (.build/ keys objects by source stem).
#include "sokol.cpp"
