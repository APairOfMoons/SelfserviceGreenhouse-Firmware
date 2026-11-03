//
// Created by APairOfMoons on 2026/11/3.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_MESSAGESENDER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_MESSAGESENDER_H
#include "communication/GreenhouseData.h"
#include "communication/GreenhouseException.h"


class MessageSender
{
    public:
    void sendMonitoringData(MonitoringData monitoringData);
    void throwGreenhouseException(GreenhouseException exception);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_MESSAGESENDER_H
