//
// Created by APairOfMoons on 2026/10/30.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_EXCEPTIONHANDLER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_EXCEPTIONHANDLER_H

enum class GreenhouseException
{
    WATER_LACK_EXCEPTION,

    TURNTABLE_FAILED_EXCEPTION
};

class ExceptionHandler
{
    public:
    void throwGreenhouseException(GreenhouseException exception);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_EXCEPTIONHANDLER_H
