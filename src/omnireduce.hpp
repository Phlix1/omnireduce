/**
  * OmniReduce project
  * author: jiawei.fei@kaust.edu.sa
  */

#pragma once

#include "context.hpp"
#include "aggcontext.hpp"

namespace omnireduce {
    int master(OmniContext* dctx);
    int aggmaster(AggContext* dctx);
}