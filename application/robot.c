#include "bsp_init.h"
#include "robot.h"
#include "robot_def.h"

#if defined(ONE_BOARD) || defined(CHASSIS_BOARD)
#include "chassis.h"
#include "referee.h"
#endif

#if defined(ONE_BOARD) || defined(GIMBAL_BOARD)
#include "gimbal.h"
#include "shoot.h"
#include "robot_cmd.h"
#endif

void RobotInit()
{
    BSPInit();

#if defined(ONE_BOARD) || defined(GIMBAL_BOARD)
    RobotCMDInit();
    GimbalInit();
    ShootInit();
#endif

#if defined(ONE_BOARD) || defined(CHASSIS_BOARD)
    Referee_Interactive_init();//syh
    ChassisInit();
#endif
}

void RobotTask()
{
#if defined(ONE_BOARD) || defined(GIMBAL_BOARD)
    RobotCMDTask();
    GimbalTask();
    ShootTask();
#endif

#if defined(ONE_BOARD) || defined(CHASSIS_BOARD)
    ChassisTask();
    Referee_Interactive_task();
#endif
}
