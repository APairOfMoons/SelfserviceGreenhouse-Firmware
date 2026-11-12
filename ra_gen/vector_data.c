/* generated vector source file - do not edit */
        #include "bsp_api.h"
        /* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
        #if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = r_icu_isr, /* ICU IRQ0 (External pin interrupt 0) */
            [1] = sci_uart_rxi_isr, /* SCI0 RXI (Receive data full) */
            [2] = sci_uart_txi_isr, /* SCI0 TXI (Transmit data empty) */
            [3] = sci_uart_tei_isr, /* SCI0 TEI (Transmit end) */
            [4] = sci_uart_eri_isr, /* SCI0 ERI (Receive error) */
            [5] = iic_master_rxi_isr, /* IIC0 RXI (Receive data full) */
            [6] = iic_master_txi_isr, /* IIC0 TXI (Transmit data empty) */
            [7] = iic_master_tei_isr, /* IIC0 TEI (Transmit end) */
            [8] = iic_master_eri_isr, /* IIC0 ERI (Transfer error) */
            [9] = sci_i2c_txi_isr, /* SCI2 TXI (Transmit data empty) */
            [10] = sci_i2c_tei_isr, /* SCI2 TEI (Transmit end) */
            [11] = adc_scan_end_isr, /* ADC0 SCAN END (End of A/D scanning operation) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_ICU_IRQ0,GROUP0), /* ICU IRQ0 (External pin interrupt 0) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_SCI0_RXI,GROUP1), /* SCI0 RXI (Receive data full) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_SCI0_TXI,GROUP2), /* SCI0 TXI (Transmit data empty) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_SCI0_TEI,GROUP3), /* SCI0 TEI (Transmit end) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_SCI0_ERI,GROUP4), /* SCI0 ERI (Receive error) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_IIC0_RXI,GROUP5), /* IIC0 RXI (Receive data full) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_IIC0_TXI,GROUP6), /* IIC0 TXI (Transmit data empty) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_IIC0_TEI,GROUP7), /* IIC0 TEI (Transmit end) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_IIC0_ERI,GROUP0), /* IIC0 ERI (Transfer error) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_SCI2_TXI,GROUP1), /* SCI2 TXI (Transmit data empty) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_SCI2_TEI,GROUP2), /* SCI2 TEI (Transmit end) */
            [11] = BSP_PRV_VECT_ENUM(EVENT_ADC0_SCAN_END,GROUP3), /* ADC0 SCAN END (End of A/D scanning operation) */
        };
        #endif
        #endif
