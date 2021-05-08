#include "omnireduce/aggcontext.hpp"
int main() {
    omnireduce::AggContext& omniContext = omnireduce::AggContext::getInstance();
    omniContext.agg_listen();
    return 0;
}
