// SPDX-License-Identifier: MIT
#pragma once

#include "bridge_protocol.h"
#include "esp_err.h"

namespace bajji {

esp_err_t device_control_start();
void device_control_receive(const bridge_frame_t* frame);
void device_control_set_ready(bool ready);

}  // namespace bajji
