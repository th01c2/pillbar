#include "model/state.hpp"

// State structs are header-defined (defaulted comparison). This translation
// unit exists so the layout engine and sources link against the same model
// and to keep the build graph stable.

namespace pillbar {

namespace {
[[maybe_unused]] const int kStateModelVersion = 1;
}

}  // namespace pillbar

