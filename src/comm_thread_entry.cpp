#include "comm_thread.h"
                /* Comm Thread entry function */
                /* pvParameters contains TaskHandle_t */
                void comm_thread_entry(void * pvParameters)
                {
                    FSP_PARAMETER_NOT_USED(pvParameters);

                    /* TODO: add your own code here */
                    while(1)
                    {
                        vTaskDelay(1);
                    }
                }
