/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the CCU8-triggered dual-group VADC
*              sampling example for ModusToolbox.
*
* Related Document: See README.md
*
*******************************************************************************
* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
******************************************************************************/

#include "cybsp.h"
#include "cy_utils.h"
#include "cy_retarget_io.h"

/*******************************************************************************
* Global Variables
*******************************************************************************/
Cy_VADC_RESULT_SIZE_t adc_result_G0Ch1 = 0;
Cy_VADC_RESULT_SIZE_t adc_result_G1Ch0 = 0;

/*******************************************************************************
* Function Name: NVIC_Config
********************************************************************************
* Summary:
* Configures the NVIC for the VADC Group 0 queue interrupt (IRQ17). Sets the
* interrupt priority and enables the IRQ.
*
* Parameters:
*  none
*
* Return:
*  none
*
*******************************************************************************/
void NVIC_Config(void)
{
    /*Set priority for IRQ*/
    NVIC_SetPriority(IRQ17_IRQn, 1u);
    /*Enable the Interrupt*/
    NVIC_EnableIRQ(IRQ17_IRQn);
}

/*******************************************************************************
* Function Name: IRQ17_Handler
********************************************************************************
* Summary:
* Interrupt handler for the VADC Group 0 queue event (IRQ17). Reads conversion
* results from Group 0 Channel 1 and Group 1 Channel 0, then clears the
* queue request-source event flag to allow retriggering.
*
* Parameters:
*  none
*
* Return:
*  none
*
*******************************************************************************/
void IRQ17_Handler(void)
{
    adc_result_G0Ch1 = Cy_VADC_GROUP_GetResult(VADC_G0, 1);
    adc_result_G1Ch0 = Cy_VADC_GROUP_GetResult(VADC_G1, 0);

    /* Acknowledge interrupt to allow retriggering. */
    Cy_VADC_GROUP_QueueClearReqSrcEvent(vadc_0_group_0_HW);
}

/*******************************************************************************
* Function Name: connect_ccu8_sr3_to_vadc_g1_background_trigger
********************************************************************************
* Summary:
* Routes the CCU8 Slice0 Compare Match Up event to SR3 and connects it as the
* VADC global background trigger with rising-edge detection.
*
* Parameters:
*  none
*
* Return:
*  none
*
*******************************************************************************/
static void connect_ccu8_sr3_to_vadc_g1_background_trigger(void)
{
    /* Route CCU8 compare match up (ch1) to SR3. */
    Cy_CCU8_SLICE_SetInterruptNode(CCU80_CC80,
                                    CY_CCU8_SLICE_IRQ_ID_COMPARE_MATCH_UP_CH_1,
                                    CY_CCU8_SLICE_SR_ID_3);
    Cy_CCU8_SLICE_EnableEvent(CCU80_CC80, CY_CCU8_SLICE_IRQ_ID_COMPARE_MATCH_UP_CH_1);

    /* Map CCU80 SR3 as the VADC background scan external trigger. */
    Cy_VADC_GLOBAL_BackgroundSelectTrigger(VADC, CY_VADC_REQ_TR_CCU80_SR3);
    Cy_VADC_GLOBAL_BackgroundSelectTriggerEdge(VADC, CY_VADC_TRIGGER_EDGE_RISING);
    Cy_VADC_GLOBAL_BackgroundEnableExternalTrigger(VADC);

    Cy_VADC_GROUP_BackgroundEnableArbitrationSlot(VADC_G1);
}

/*******************************************************************************
* Function Name: force_vadc_g1_queue_external_trigger
********************************************************************************
* Summary:
* Overrides the Device Configurator queue settings for VADC Group 1 to enable
* external trigger mode. Flushes stale entries and inserts a single
* external-triggered channel entry.
*
* Parameters:
*  none
*
* Return:
*  none
*
*******************************************************************************/
static void force_vadc_g1_queue_external_trigger(void)
{
    Cy_VADC_QUEUE_ENTRY_t g1_queue_entry =
    {
        .channel_num = (uint8_t)0,
        .refill_needed = (uint32_t)true,
        .generate_interrupt = (uint32_t)false,
        .external_trigger = (uint32_t)true,
    };

    Cy_VADC_GROUP_QueueSelectTrigger(VADC_G1, (Cy_VADC_TRIGGER_INPUT_SELECT_t)VADC0_BGXTSEL_VALUE);
    Cy_VADC_GROUP_QueueSelectTriggerEdge(VADC_G1, CY_VADC_TRIGGER_EDGE_RISING);
    Cy_VADC_GROUP_QueueSelectGating(VADC_G1, (Cy_VADC_GATE_INPUT_SELECT_t)VADC0_BGGTSEL_VALUE);
    Cy_VADC_GROUP_QueueSetGatingMode(VADC_G1, CY_VADC_GATEMODE_IGNORE);
    Cy_VADC_GROUP_QueueEnableExternalTrigger(VADC_G1);

    /* Flush stale entries and insert one external-triggered entry. */
    Cy_VADC_GROUP_QueueFlushEntries(VADC_G1);
    Cy_VADC_GROUP_QueueInsertChannel(VADC_G1, g1_queue_entry);

    Cy_VADC_GROUP_QueueDisableArbitrationSlot(VADC_G1);
}

/*******************************************************************************
* Function Name: set_g1_trigger_enabled
********************************************************************************
* Summary:
* Enables or disables the CCU8 compare-match event and VADC Group 1 queue
* arbitration slot together to gate triggered conversions.
*
* Parameters:
*  enable - true to enable triggering, false to disable
*
* Return:
*  none
*
*******************************************************************************/
static void set_g1_trigger_enabled(bool enable)
{
    if (enable)
    {
        Cy_CCU8_SLICE_EnableEvent(ADC_TRIG_HW, CY_CCU8_SLICE_IRQ_ID_COMPARE_MATCH_UP_CH_1);
        Cy_VADC_GROUP_QueueEnableArbitrationSlot(VADC_G1);
    }
    else
    {
        Cy_CCU8_SLICE_DisableEvent(ADC_TRIG_HW, CY_CCU8_SLICE_IRQ_ID_COMPARE_MATCH_UP_CH_1);
        Cy_VADC_GROUP_QueueDisableArbitrationSlot(VADC_G1);
    }
}

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* Entry point of the application. Initializes the board peripherals and both
* VADC groups, configures the CCU8-to-VADC trigger path, sets up the NVIC,
* and initializes retarget-IO. The main loop prints ADC results over UART.
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* cybsp_init does not set group power mode; groups must be initialized explicitly. */
    Cy_VADC_GROUP_Init(vadc_0_group_0_HW, &vadc_0_group0_init_config);
    Cy_VADC_GROUP_InputClassInit(vadc_0_group_0_HW, vadc_0_0_iclass_0, CY_VADC_GROUP_CONV_STD, 0U);
    Cy_VADC_GROUP_InputClassInit(vadc_0_group_0_HW, vadc_0_0_iclass_1, CY_VADC_GROUP_CONV_STD, 1U);
    Cy_VADC_GROUP_SetPowerMode(vadc_0_group_0_HW, CY_VADC_GROUP_POWERMODE_NORMAL);

    Cy_VADC_GROUP_Init(vadc_0_group_1_HW, &vadc_0_group1_init_config);
    Cy_VADC_GROUP_InputClassInit(vadc_0_group_1_HW, vadc_0_1_iclass_0, CY_VADC_GROUP_CONV_STD, 0U);
    Cy_VADC_GROUP_InputClassInit(vadc_0_group_1_HW, vadc_0_1_iclass_1, CY_VADC_GROUP_CONV_STD, 1U);
    Cy_VADC_GROUP_SetPowerMode(vadc_0_group_1_HW, CY_VADC_GROUP_POWERMODE_NORMAL);

    /* Device Configurator does not set external trigger mode for Group 1 queue. */
    force_vadc_g1_queue_external_trigger();
    set_g1_trigger_enabled(true);

    /* Connect CCU8 SR3 as the background trigger for VADC. */
    connect_ccu8_sr3_to_vadc_g1_background_trigger();

    Cy_VADC_GROUP_QueueSetReqSrcEventInterruptNode(vadc_0_group_0_HW, (Cy_VADC_SR_t) CY_VADC_SR_GROUP_SR0);

    /* Map Group 0 SR0 to NVIC IRQ17. */
    Cy_SCU_SetInterruptControl(IRQ17_IRQn, CY_SCU_IRQCTRL_VADC0_G0SR0_IRQ17);

    NVIC_Config();

    cy_retarget_io_init(CYBSP_DEBUG_UART_HW);

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");
    printf("** PSOC C1 : VADC Multi-Trigger Conversion **\r\n\n");

    for (;;)
    {
        printf("G0CH1: %u, G1CH0: %u\r\n", adc_result_G0Ch1, adc_result_G1Ch0);
        Cy_Delay(100);
    }
}

/* [] END OF FILE */
