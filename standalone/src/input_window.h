#pragma once
#include "input.h"
#include <functional>
void draw_input_settings(Input &input, const std::function<void(unsigned)> &draw_pak = {});
