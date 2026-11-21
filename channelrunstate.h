#ifndef CHANNELRUNSTATE_H
#define CHANNELRUNSTATE_H

/**
 * 通道运行状态。
 *
 * - Idle：空闲（已启用，未测试）
 * - Testing：测试中
 * - Abnormal：异常
 * - Disabled：通道禁用
 *
 * ModuleRackWidget / ModuleOverviewWidget 提供对应的读取与设置接口；
 * 禁用状态同时由 ChannelState::enabled 决定，即使内部记录为 Idle，
 * 读取时也会返回 Disabled。
 */
enum class ChannelRunState {
    Idle = 0,
    Testing = 1,
    Abnormal = 2,
    Disabled = 3
};

#endif // CHANNELRUNSTATE_H
