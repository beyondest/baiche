#include "HT04.h"
#include "memory.h"
#include "general_def.h"

static uint8_t idx;
HTMotorInstance *ht_motor_instance[HT_MOTOR_CNT];

/**
 * @brief
 *
 * @param cmd
 * @param motor
 */
static void HTMotorSetMode(HTMotor_Mode_t cmd, HTMotorInstance *motor)
{
    static uint8_t buf[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    buf[7] = (uint8_t)cmd;
    memcpy(motor->motor_can_instace->tx_buff, buf, sizeof(buf));
    CANTransmit(motor->motor_can_instace);
}

/* 两个用于将uint值和float值进行映射的函数,在设定发送值和解析反馈值时使用 */
static uint16_t float_to_uint(float x, float x_min, float x_max, uint8_t bits)
{
    float span = x_max - x_min;
    float offset = x_min;
    return (uint16_t)((x - offset) * ((float)((1 << bits) - 1)) / span);
}
static float uint_to_float(int x_int, float x_min, float x_max, int bits)
{
    float span = x_max - x_min;
    float offset = x_min;
    return ((float)x_int) * span / ((float)((1 << bits) - 1)) + offset;
}

/**
 * @brief 解析电机反馈值
 *
 * @param motor_can 收到
 */
static void HTMotorDecode(CANInstance *motor_can)
{
    static uint16_t tmp; // 用于暂存解析值,稍后转换成float数据,避免多次创建临时变量
    static HTMotor_Measure_t *measure;
    static uint8_t *rxbuff;

    rxbuff = motor_can->rx_buff;
    measure = &((HTMotorInstance *)motor_can->id)->motor_measure; // 将can实例中保存的id转换成电机实例的指针

    measure->last_angle = measure->total_angle;

    tmp = (uint16_t)((rxbuff[1] << 8) | rxbuff[2]);
    measure->total_angle = RAD_2_ANGLE * uint_to_float(tmp, P_MIN, P_MAX, 16);

    tmp = (uint16_t)((rxbuff[3] << 4) | (rxbuff[4] >> 4));
    measure->speed_aps = RAD_2_ANGLE * SPEED_SMOOTH_COEF * uint_to_float(tmp, V_MIN, V_MAX, 12) +
                         (1 - SPEED_SMOOTH_COEF) * measure->speed_aps;

    tmp = (uint16_t)(((rxbuff[4] & 0x0f) << 8) | rxbuff[5]);
    measure->real_current = CURRENT_SMOOTH_COEF * uint_to_float(tmp, T_MIN, T_MAX, 12) +
                            (1 - CURRENT_SMOOTH_COEF) * measure->real_current;
}

HTMotorInstance *HTMotorInit(Motor_Init_Config_s *config)
{
    HTMotorInstance *motor = (HTMotorInstance *)malloc(sizeof(HTMotorInstance));
    memset(motor, 0, sizeof(HTMotorInstance));

    motor->motor_settings = config->controller_setting_init_config;
    PID_Init(&motor->current_PID, &config->controller_param_init_config.current_PID);
    PID_Init(&motor->speed_PID, &config->controller_param_init_config.speed_PID);
    PID_Init(&motor->angle_PID, &config->controller_param_init_config.angle_PID);
    motor->other_angle_feedback_ptr = config->controller_param_init_config.other_angle_feedback_ptr;
    motor->other_speed_feedback_ptr = config->controller_param_init_config.other_speed_feedback_ptr;

    config->can_init_config.can_module_callback = HTMotorDecode;
    config->can_init_config.id = motor;
    motor->motor_can_instace = CANRegister(&config->can_init_config);

    HTMotorEnable(motor);
    ht_motor_instance[idx++] = motor;
    return motor;
}

void HTMotorSetRef(HTMotorInstance *motor, float ref)
{
    motor->pid_ref = ref;
}

void HTMotorControl()
{
    static float set, pid_measure, pid_ref;
    static uint16_t tmp;
    static HTMotorInstance *motor;
    static HTMotor_Measure_t *measure;
    static Motor_Control_Setting_s *setting;
    static CANInstance *motor_can;

    // 遍历所有电机实例,计算PID
    for (size_t i = 0; i < idx; i++)
    { // 先获取地址避免反复寻址
        motor = ht_motor_instance[i];
        measure = &motor->motor_measure;
        setting = &motor->motor_settings;
        motor_can = motor_can;
        pid_ref = motor->pid_ref;

        if ((setting->close_loop_type & ANGLE_LOOP) && setting->outer_loop_type == ANGLE_LOOP)
        {
            if (setting->angle_feedback_source == OTHER_FEED)
                pid_measure = *motor->other_angle_feedback_ptr;
            else
                pid_measure = measure->real_current;
            pid_ref = PID_Calculate(&motor->angle_PID, pid_measure, pid_ref);
            if (setting->feedforward_flag & SPEED_FEEDFORWARD)
                pid_ref += *motor->speed_feedforward_ptr;
        }

        if ((setting->close_loop_type & SPEED_LOOP) && setting->outer_loop_type & (ANGLE_LOOP | SPEED_LOOP))
        {
            if (setting->angle_feedback_source == OTHER_FEED)
                pid_measure = *motor->other_speed_feedback_ptr;
            else
                pid_measure = measure->speed_aps;
            pid_ref = PID_Calculate(&motor->angle_PID, pid_measure, pid_ref);
            if (setting->feedforward_flag & CURRENT_FEEDFORWARD)
                pid_ref += *motor->current_feedforward_ptr;
        }

        if (setting->close_loop_type & CURRENT_LOOP)
        {
            pid_ref = PID_Calculate(&motor->current_PID, measure->real_current, pid_ref);
        }

        set = pid_ref;
        if (setting->reverse_flag == MOTOR_DIRECTION_REVERSE)
            set *= -1;

        tmp = float_to_uint(set, T_MIN, T_MAX, 12);
        motor_can->tx_buff[6] = tmp >> 8;
        motor_can->tx_buff[7] = tmp & 0xff;

        if (motor->stop_flag == MOTOR_STOP)
        { // 若该电机处于停止状态,直接将发送buff置零
            memset(motor_can->tx_buff + 6, 0, sizeof(uint16_t));
        }
        CANTransmit(motor_can);
    }
}

void HTMotorStop(HTMotorInstance *motor)
{
    HTMotorSetMode(CMD_RESET_MODE, motor);
}

void HTMotorEnable(HTMotorInstance *motor)
{
    HTMotorSetMode(CMD_MOTOR_MODE, motor);
}

void HTMotorCalibEncoder(HTMotorInstance *motor)
{
    HTMotorSetMode(CMD_ZERO_POSITION, motor);
}