/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HWC_COPYBIT_CLEAR_H
#define HWC_COPYBIT_CLEAR_H

#include <copybit.h>
#include "gralloc_priv.h"
#include "gr.h"

using namespace qhwc;

static inline int clearRenderBuffer(copybit_device_t *copybit,
                                    private_handle_t *renderBuffer) {
    if (!copybit || !renderBuffer)
        return -1;

    copybit_rect_t clear_rect = {0, 0,
        ALIGN(getWidth(renderBuffer), 32),
        getHeight(renderBuffer)};

    copybit_image_t buf;
    buf.w = ALIGN(getWidth(renderBuffer), 32);
    buf.h = getHeight(renderBuffer);
    buf.format = renderBuffer->format;
    buf.base = (void *)renderBuffer->base;
    buf.handle = (native_handle_t *)renderBuffer;

    return copybit->clear(copybit, &buf, &clear_rect);
}

#endif // HWC_COPYBIT_CLEAR_H
