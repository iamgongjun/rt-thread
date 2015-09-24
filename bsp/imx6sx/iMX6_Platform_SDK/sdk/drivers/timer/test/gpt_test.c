/*
 * Copyright (c) 2011-2012, Freescale Semiconductor, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of Freescale Semiconductor, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*!
 * @file gpt_test.c
 * @brief GPT unit tests source file.
 *
 * @ingroup diag_timer
 */

#include <stdio.h>
#include "timer/gpt.h"
#include "timer/timer.h"
#include "gpt_test.h"
#include "registers/regsiomuxc.h"

static volatile uint8_t g_capture_event, g_compare_event, g_rollover_event;
static uint32_t g_counter_val;
static uint32_t g_test;

/*!
 * @brief Main unit test for the GPT.
 * @return  0
 */
int32_t gpt_test(void)
{
    uint8_t sel;

    printf("Start GPT unit tests:");

    do {

        printf("\n  1 - for output compare test.\n");
        printf("  2 - for input capture test.\n");
        printf("  x - to exit.\n\n");

        do {
            sel = getchar();
        } while (sel == (uint8_t) NONE_CHAR);

        if (sel == 'x') {
            printf("\nTest exit.\n");
            break;
        }

        if (sel == '1')
            gpt_out_compare_test();
        if (sel == '2')
            gpt_in_capture_test();

    } while(1);

    return 0;
}

/*!
 * @brief Output compare test.
 *
 * This test enables the 3 compare channels. A first event occurs after 1s,
 * the second occurs after 2s, and the third after 3s. That last event is
 * generated by the compare channel 1 which is the only one that can restart
 * the counter to 0x0 after an event.
 * This restarts for a programmed number of seconds.
 * Output compare I/Os are not enabled in this test, though it would simply
 * require to configure the IOMUX settings and enable the feature.
 */
void gpt_out_compare_test(void)
{
    uint32_t counter = 0;
    // stops after xx seconds 
    uint32_t max_iteration = 4*3;
    uint32_t freq = 0;

    printf("GPT is programmed to generate an interrupt once a compare event occured.\n");
    printf("The test exists after %d seconds.\n",max_iteration);
    g_test = 1;

    // Initialize the GPT timer 
    // The source clock for the timer will be configured to be IPG_CLK, so
    // the GPT frequency is filled first with the IPG_CLK frequency.
    freq = get_main_clock(IPG_CLK);
    
    // IPG_CLK is in MHz (usually 66Mhz), so divide it to get a reference
    // clock of 1MHz => 1us per count
    gpt_init(CLKSRC_IPG_CLK, freq/1000000, RESTART_MODE, WAIT_MODE_EN | STOP_MODE_EN);
    gpt_setup_interrupt(gpt_interrupt_routine, TRUE);

    // set a first compare event after 1s 
    gpt_set_compare_event(kGPTOutputCompare3, OUTPUT_CMP_DISABLE, 1000000);
    
    // set a second compare event after 2s 
    gpt_set_compare_event(kGPTOutputCompare2, OUTPUT_CMP_DISABLE, 2000000);
    
    // set a third compare event after 3s, which restarts the counter as
    // this event is generated by compare channel 1
    gpt_set_compare_event(kGPTOutputCompare1, OUTPUT_CMP_DISABLE, 3000000);
    
    // enable the IRQ for each event 
    gpt_counter_enable(kGPTOutputCompare1 | kGPTOutputCompare2 | kGPTOutputCompare3);

    while (counter != max_iteration)
    {
        g_compare_event = 0;
        while (g_compare_event == 0);
        counter++;
        printf("Elapsed time %d seconds. g_compare_event = 0x%x\n", counter, g_compare_event);
    };

    gpt_counter_disable();
}

/*! 
 * @brief GPT unit test interrupt handler.
 */
void gpt_interrupt_routine(void)
{
    if (g_test == 1)
    {
        g_compare_event = gpt_get_compare_event(kGPTOutputCompare1 | kGPTOutputCompare2 | kGPTOutputCompare3);
    }
    else if (g_test == 2)
    {
        // if this is a capture event => clear the flag 
        g_capture_event = gpt_get_capture_event(kGPTInputCapture2, &g_counter_val);
        
        // if this is a rollover event => clear the flag 
        g_rollover_event = gpt_get_rollover_event();
    }
}

/*!
 * @brief Input capture test.
 *
 * This test enables an input capture. An I/O is used to monitor an event that
 * stores the counter value into a GPT input capture register when it occurs.
 * The test simply display the amount of time elapsed since the test was started
 * and the moment the capture was done. It uses the rollover interrupt event too,
 * because if the counter is used for a sufficient time, it will rollover.
 * That information is requested to calculate the exact number of seconds.
 *
 * The input CAPIN2 available as ALT3 for SD1_DAT1 is used. To generate an event,
 * this signal must be tied to a low level.
 */
void gpt_in_capture_test(void)
{
    uint32_t counter = 0;
    // stops after a timeout of 5 rollover 
    uint32_t timeout = 5;
    uint32_t freq = 0;

    printf("The GPT is programmed to generate an interrupt once a capture event occured.\n");
    printf("Please pull the CAPIN2 signal low to generate an event.\n");
    printf("The text exits after a capture event or a timeout of %d rollover ~ 5min25sec.\n", timeout);
    g_test = 2;

    // Config gpt.GPT_CAPTURE2 to pad SD1_DATA1(C20)
    // HW_IOMUXC_SW_MUX_CTL_PAD_SD1_DATA1_WR(0x00000003);
    // HW_IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1_WR(0x0001B0B0); - use default reset value
    HW_IOMUXC_SW_MUX_CTL_PAD_SD1_DATA1_WR(
            BF_IOMUXC_SW_MUX_CTL_PAD_SD1_DATA1_SION_V(DISABLED) |
            BF_IOMUXC_SW_MUX_CTL_PAD_SD1_DATA1_MUX_MODE_V(ALT3));
    HW_IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1_WR(
            BF_IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1_HYS_V(ENABLED) |
            BF_IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1_PUS_V(100K_OHM_PU) |
            BF_IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1_PUE_V(PULL) |
            BF_IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1_PKE_V(ENABLED) |
            BF_IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1_ODE_V(DISABLED) |
            BF_IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1_SPEED_V(100MHZ) |
            BF_IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1_DSE_V(40_OHM) |
            BF_IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1_SRE_V(SLOW));

    // Initialize the GPT timer 
    // The source clock for the timer will be configured to be IPG_CLK, so
    // the GPT frequency is filled first with the IPG_CLK frequency.
    freq = get_main_clock(IPG_CLK);
    
    // IPG_CLK is in MHz (usually 66Mhz), so divide it to get a reference
    // clock of 1MHz => 1us per count
    gpt_init(CLKSRC_IPG_CLK, 1, RESTART_MODE, WAIT_MODE_EN | STOP_MODE_EN);
    gpt_setup_interrupt(gpt_interrupt_routine, TRUE);

    // set the capture mode to falling edge on kGPTInputCapture2
    gpt_set_capture_event(kGPTInputCapture2, INPUT_CAP_FALLING_EDGE);
    
    // enable the IRQ for each event 
    gpt_counter_enable(kGPTInputCapture2 | kGPTRollover);

    g_capture_event = 0;
    g_rollover_event = 0;
    while (counter != timeout)
    {
        hal_delay_us(1000);
        while ((g_capture_event == 0) && (g_rollover_event == 0));
        if (g_capture_event)
        {
            printf("Time between start and event = %d seconds\n",
                   g_counter_val/freq + counter*(0xFFFFFFFF/freq));
            g_capture_event = 0;
            break;
        }
        else // necessary a rollover event 
        {
            counter++;
            printf("Rollover occured %d times!\n", counter);
            g_rollover_event = 0;
        }
    };

    gpt_counter_disable();
}