#pragma once
#include "test_framework.h"

namespace restir::test {

void test_rng(Context& ctx);
void test_wrs(Context& ctx);
void test_reservoir_merge(Context& ctx);
void test_reconnection(Context& ctx);
void test_visibility(Context& ctx);
void test_bvh(Context& ctx);
void test_render_sanity(Context& ctx);
void test_lighting(Context& ctx);

} // namespace restir::test
