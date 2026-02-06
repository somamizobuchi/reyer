#pragma once

#include "reyer/core/thread.hpp"

namespace reyer_rt::threading {

template <typename Derived>
using Thread = reyer::core::Thread<Derived>;

} // namespace reyer_rt::threading
