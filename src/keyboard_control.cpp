/*
 * =====================================================================================
 *        COPYRIGHT NOTICE
 *        Copyright (c) 2013  HUST-Renesas Lab
 *        ALL rights reserved.
 *//**        
 *        @file     keyboard.cpp
 *        @brief    robot keyboard control
 *        @version  0.1
 *        @date     2013/5/23 15:22:40
 *        @author   Hu Chunxu , huchunxu@hust.edu.cn
 *//* ==================================================================================
 *  @0.1    Hu Chunxu    2013/5/23 15:22:40   create orignal file
 * =====================================================================================
 */

#include <termios.h>
#include <signal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>

#include <boost/thread/thread.hpp>
#include <ros/ros.h>
#include <geometry_msgs/Twist.h>

#include <omp.h>

#define KEYCODE_1 0x31
#define KEYCODE_2 0x32
#define KEYCODE_3 0x33
#define KEYCODE_4 0x34
#define KEYCODE_5 0x35
#define KEYCODE_6 0x36
#define KEYCODE_7 0x37
#define KEYCODE_8 0x38
#define KEYCODE_9 0x39


#define KEYCODE_W 0x77
#define KEYCODE_A 0x61
#define KEYCODE_S 0x73
#define KEYCODE_D 0x64
#define KEYCODE_X 0x78
#define KEYCODE_J 0x6A
#define KEYCODE_K 0x6B


/* 带有shift键 */
#define KEYCODE_W_CAP 0x57
#define KEYCODE_A_CAP 0x41
#define KEYCODE_S_CAP 0x53
#define KEYCODE_D_CAP 0x44
#define KEYCODE_X_CAP 0x58
#define KEYCODE_J_CAP 0x4A
#define KEYCODE_K_CAP 0x4B


class SmartCarKeyboardTeleopNode
{
    private:
        double walk_vel_;
        double run_vel_;
        double yaw_rate_;
        double yaw_rate_run_;
        
        geometry_msgs::Twist cmdvel_;
        ros::NodeHandle n_;
        ros::Publisher pub_;

        std::chrono::_V2::steady_clock::time_point start = std::chrono::steady_clock::now();

    public:
        SmartCarKeyboardTeleopNode()
        {
            pub_ = n_.advertise<geometry_msgs::Twist>("cmd_vel_", 1);
            
            ros::NodeHandle n_private("~");
            n_private.param("walk_vel", walk_vel_, 0.2);
            n_private.param("run_vel", run_vel_, 0.5);
            n_private.param("yaw_rate", yaw_rate_, 0.5);
            n_private.param("yaw_rate_run", yaw_rate_run_, 1.0);
        }
        
        ~SmartCarKeyboardTeleopNode() { }
        void keyboardLoop();
        
        void stopRobot()
        {
            geometry_msgs::Twist zero_vel;
            pub_.publish(zero_vel);
        }
};

SmartCarKeyboardTeleopNode* tbk;

/**
 * 文件描述符
 * 内核（kernel）利用文件描述符（file descriptor）来访问文件。文件描述符是非负整数。
 * 标准输入（standard input）的文件描述符是 0，标准输出（standard output）是 1，标准错误（standard error）是 2。
 */
int kfd = 0;

/** 
 *  === struct termios ===
 *  tcflag_t c_iflag;  输入模式
 *　tcflag_t c_oflag;  输出模式 
 *　tcflag_t c_cflag;  控制模式 
 *  tcflag_t c_lflag;  本地模式
 *  cc_t c_cc[NCCS];   控制字符 
 */
struct termios cooked, raw;
bool done;

int main(int argc, char** argv)
{
    ros::init(argc,argv,"tbk", ros::init_options::AnonymousName | ros::init_options::NoSigintHandler);
    SmartCarKeyboardTeleopNode tbk;

	/* 创建一个新的线程 */
    boost::thread t = boost::thread(boost::bind(&SmartCarKeyboardTeleopNode::keyboardLoop, &tbk));
    
    ros::spin();
    
    t.interrupt();
    t.join();
    tbk.stopRobot();

	/* 设置终端参数 */
    tcsetattr(kfd, TCSANOW, &cooked);
    
    return(0);
}

void SmartCarKeyboardTeleopNode::keyboardLoop()
{
    char c;
    double max_tv = walk_vel_;
    double max_rv = yaw_rate_;
    bool dirty = false;
    int speed = 0;
    int turn = 0;
    double last_ctrl_time = omp_get_wtime();
    
    /** 
	 * 从终端中获取按键 
	 * int tcgetattr(int fd, struct termios *termios_p);
	 */
    tcgetattr(kfd, &cooked);
    memcpy(&raw, &cooked, sizeof(struct termios));

    /**
	 * c_lflag : 本地模式标志，控制终端编辑功能
	 * ICANON: 使用标准输入模式
	 * ECHO: 显示输入字符
	 */
    raw.c_lflag &=~ (ICANON | ECHO);

	/** 
	 * c_cc[NCCS]：控制字符，用于保存终端驱动程序中的特殊字符，如输入结束符等
	 * VEOL: 附加的End-of-file字符
	 * VEOF: End-of-file字符
	 * */
    raw.c_cc[VEOL] = 1;
    raw.c_cc[VEOF] = 2;
    tcsetattr(kfd, TCSANOW, &raw);
    
    puts("Reading from keyboard");
    puts("Use WASD keys to control the robot");
    puts("Press Shift to move faster");
    
	/* *
	 * struct pollfd {
　　       int fd;        文件描述符 
　       　short events;  等待的事件 
　　       short revents; 实际发生了的事件 
　       　};
    */
    struct pollfd ufd;
    ufd.fd = kfd;
    ufd.events = POLLIN;
    
    for(;;)
    {
       
        boost::this_thread::interruption_point();
        
        int num;
        
		/**
		 * poll:把当前的文件指针挂到设备内部定义的等待队列中。
		 * unsigned int (*poll)(struct file * fp, struct poll_table_struct * table)
		 */
        if ((num = poll(&ufd, 1, 250)) < 0)
        {
			/**
			 * perror( ) 用来将上一个函数发生错误的原因输出到标准设备(stderr)。
			 * 参数s所指的字符串会先打印出,后面再加上错误原因字符串。
			 * 此错误原因依照全局变量errno 的值来决定要输出的字符串。
			 * */
            perror("poll():");
            return;
        }
        else if(num > 0)
        {
            if(read(kfd, &c, 1) < 0)
            {
                perror("read():");
                return;
            }
        }
        else
        {
            // num == 0 说明没有输入，直接跳过
            continue;
        }

        // 控制一下发布频率
        if (omp_get_wtime() - last_ctrl_time < 0.1)
        {
            // 时间还没到该发布速度的时候，就直接跳过。
            continue;
        }

        switch(c)
        {
            case KEYCODE_W:
                cmdvel_.linear.x =  walk_vel_;
                cmdvel_.linear.y =  0.0;
                cmdvel_.angular.z = 0.0;
                break;
            case KEYCODE_S:
                cmdvel_.linear.x =  0.0;
                cmdvel_.linear.y =  0.0;
                cmdvel_.angular.z = 0.0;
                break;
            case KEYCODE_A:
                cmdvel_.linear.x =  0.0;
                cmdvel_.linear.y =  walk_vel_;
                cmdvel_.angular.z = 0.0;
                break;
            case KEYCODE_D:
                cmdvel_.linear.x =  0.0;
                cmdvel_.linear.y =  -walk_vel_;
                cmdvel_.angular.z = 0.0;
                break;
            case KEYCODE_X:
                cmdvel_.linear.x =  -walk_vel_;
                cmdvel_.linear.y =  0.0;
                cmdvel_.angular.z = 0.0;
                break;
            case KEYCODE_W_CAP:
                cmdvel_.linear.x =  run_vel_;
                cmdvel_.linear.y =  0.0;
                cmdvel_.angular.z = 0.0;
                break;
            case KEYCODE_S_CAP:
                cmdvel_.linear.x =  0.0;
                cmdvel_.linear.y =  0.0;
                cmdvel_.angular.z = 0.0;
                break;
            case KEYCODE_A_CAP:
                cmdvel_.linear.x =  0.0;
                cmdvel_.linear.y =  run_vel_;
                cmdvel_.angular.z = 0.0;
                break;
            case KEYCODE_D_CAP:
                cmdvel_.linear.x =  0.0;
                cmdvel_.linear.y =  -run_vel_;
                cmdvel_.angular.z = 0.0;
                break;
            case KEYCODE_X_CAP:
                cmdvel_.linear.x =  -run_vel_;
                cmdvel_.linear.y =  0.0;
                cmdvel_.angular.z = 0.0;
                break;

            case KEYCODE_J:
                cmdvel_.linear.x =  0.0;
                cmdvel_.linear.y =  0.0;
                cmdvel_.angular.z = walk_vel_;
                break;
            case KEYCODE_K:
                cmdvel_.linear.x =  0.0;
                cmdvel_.linear.y =  0.0;
                cmdvel_.angular.z = -walk_vel_;
                break;
            case KEYCODE_J_CAP:
                cmdvel_.linear.x =  0.0;
                cmdvel_.linear.y =  0.0;
                cmdvel_.angular.z = run_vel_;
                break;
            case KEYCODE_K_CAP:
                cmdvel_.linear.x =  0.0;
                cmdvel_.linear.y =  0.0;
                cmdvel_.angular.z = -run_vel_;
                break;
            default:
                cmdvel_.linear.x =  0.0;
                cmdvel_.linear.y =  0.0;
                cmdvel_.angular.z = 0.0;
        }
        
        last_ctrl_time = omp_get_wtime();
        pub_.publish(cmdvel_);
    }
}

