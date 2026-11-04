//
// ControllerSnapshot -> MonitoringData 的上报转换。
//
// 两个结构体的字段名已对齐（temperature / air_humidity / soil_humidity /
// illuminance / has_enough_water / fan_on / light_on / heater_on），
// 这里只做类型转换：float -> double、uint16_t -> int。
//
// 注意：MonitoringData 里没有水泵、转盘角度、故障位，这些需要通过
// 扩展报文或 MessageSender 的异常通道单独上报。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_MONITORINGDATAMAPPER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_MONITORINGDATAMAPPER_H

#include "communication/GreenhouseData.h"
#include "logic/LogicController.h"

namespace greenhouse {

// 转换成功返回 true；out_data 为空返回 false。
bool toMonitoringData(const ControllerSnapshot &snapshot, MonitoringData *out_data);

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_MONITORINGDATAMAPPER_H
