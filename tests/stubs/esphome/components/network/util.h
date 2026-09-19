#pragma once
#include "../../../fake.h"
namespace esphome { namespace network { inline bool is_connected() { return fake::connected; } } }
