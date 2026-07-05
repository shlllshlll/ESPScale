#pragma once

#include "esp_err.h"
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t http_client_post_weight(const weight_data_t *data);

#ifdef __cplusplus
}
#endif
