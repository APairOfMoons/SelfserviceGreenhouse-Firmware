#include "comm_thread.h"

#include "communication/app/CommunicationService.h"

/* Comm Thread entry function */
/* pvParameters contains TaskHandle_t */
extern "C" void comm_thread_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* 线程主循环，不返回 */
    CommunicationService::run();
}
