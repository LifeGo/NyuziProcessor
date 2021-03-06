//
// Copyright 2011-2015 Jeff Bush
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

//
// Parallel mandelbrot set renderer. The screen is divided between threads
// by scanline. Each thread uses its vector unit to compute the values of
// 16 values at a time. This means there are 64 pixels being computed at any
// time.
//

#include <nyuzi.h>
#include <schedule.h>
#include <stdint.h>
#include <stdio.h>
#include <vga.h>

#define mask_cmpf_lt __builtin_nyuzi_mask_cmpf_lt
#define mask_cmpi_ult __builtin_nyuzi_mask_cmpi_ult
#define mask_cmpi_uge __builtin_nyuzi_mask_cmpi_uge
#define vector_mixi __builtin_nyuzi_vector_mixi

const int kMaxIterations = 255;
const int kScreenWidth = 640;
const int kScreenHeight = 480;
const float kXStep = 2.5 / kScreenWidth;
const float kYStep = 2.0 / kScreenHeight;
const int kNumThreads = 4;
const int kVectorLanes = 16;
volatile int stopCount = 0;
char *fbBase;
volatile int gThreadId = 0;

// All threads start execution here.
int main()
{
    int myThreadId = __sync_fetch_and_add(&gThreadId, 1);
    if (myThreadId == 0)
    {
        fbBase = init_vga(VGA_MODE_640x480);
        start_all_threads();
    }

    vecf16_t kInitialX0 = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    kInitialX0 = kInitialX0 * kXStep - 2.0;

    // Stagger row access by thread ID
    for (int row = myThreadId; row < kScreenHeight; row += kNumThreads)
    {
        veci16_t *ptr = (veci16_t*)(fbBase + row * kScreenWidth * 4);
        vecf16_t x0 = kInitialX0;
        float y0 = kYStep * row - 1.0;
        for (int col = 0; col < kScreenWidth; col += kVectorLanes)
        {
            // Compute colors for 16 pixels
            vecf16_t x = 0.0;
            vecf16_t y = 0.0;
            veci16_t iteration = 0;
            int activeLanes = 0xffff;

            // Escape loop
            while (1)
            {
                vecf16_t xSquared = x * x;
                vecf16_t ySquared = y * y;
                activeLanes &= mask_cmpf_lt(xSquared + ySquared, (vecf16_t) 4.0);
                activeLanes &= mask_cmpi_ult(iteration, (veci16_t) kMaxIterations);
                if (!activeLanes)
                    break;

                y = x * y * 2.0 + y0;
                x = xSquared - ySquared + x0;
                iteration = vector_mixi(activeLanes, iteration + 1, iteration);
            }

            // Set pixels inside set black and increase contrast
            *ptr = vector_mixi(mask_cmpi_uge(iteration, (veci16_t) 255),
                    (veci16_t) 0, (iteration << 2) + 80) | (veci16_t) 0xff000000;
            asm("dflush %0" : : "s" (ptr++));
            x0 += kXStep * kVectorLanes;
        }
    }

    // Wait for other threads, because returning from main will kill all of them.
    __sync_fetch_and_add(&stopCount, 1);
    while (stopCount != 4)
        ;
}
