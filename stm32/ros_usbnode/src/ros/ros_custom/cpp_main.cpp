/*
 * C++ Main
 *
 * contains the rosserial handlers
 *
 */

#include "board.h"

#include <cpp_main.h>
#include "main.h"
#include "panel.h"
#include "stm32f1xx_hal.h"
#include "ringbuffer.h"
#include "ros.h"
#include "ros/time.h"
#include "tf/tf.h"
#include "tf/transform_broadcaster.h"
#include "std_msgs/Bool.h"
#include "std_msgs/String.h"
#include "std_msgs/Float32.h"
#include "std_msgs/Int16.h"
#include "std_msgs/UInt16.h"
#include "nav_msgs/Odometry.h"
#include "nbt.h"
#include "geometry_msgs/Twist.h"



#define MAX_MPS	  	0.6		 	// Allow maximum speed of 0.6 m/s 
#define PWM_PER_MPS 300.0		// PWM value of 300 means 1 m/s bot speed

#define WHEEL_BASE  0.325		// The distance between the center of the wheels in meters
#define WHEEL_DIAMETER 0.198 	// The diameter of the wheels in meters

#define BROADCAST_NBT_TIME_MS 50 	// 50ms interval where we set drive motors and read back values

extern uint8_t RxBuffer[RxBufferSize];
struct ringbuffer rb;

// drive motor control
static uint8_t left_speed=0;
static uint8_t right_speed=0;
static uint8_t left_dir=0;
static uint8_t right_dir=0;

// blade motor control
static uint8_t blade_on_off=0;

ros::NodeHandle nh;

// TF
geometry_msgs::Quaternion quat;
geometry_msgs::TransformStamped t;
tf::TransformBroadcaster broadcaster;
char base_link[] = "base_link";
char odom[] = "odom";

//double radius = 0.04;                              //Wheel radius, in m
//double wheelbase = 0.187;                          //Wheelbase, in m
double two_pi = 6.28319;
double speed_act_left = 0.0;
double speed_act_right = 0.0;
double speed_req1 = 0.0;
double speed_req2 = 0.0;
double speed_dt = 0.0;
double x_pos = 0.0;
double y_pos = 0.0;
double theta = 0.0;
ros::Time current_time;
// ros::Time speed_time;
// ---
// double rate = 10.0;
double linear_scale_positive = 1.0;
double linear_scale_negative = 1.0;
double angular_scale_positive = 1.0;
double angular_scale_negative = 1.0;
bool publish_tf = true; // publish odom -> base_link transform
double dt = 0.0;
double dx = 0.0;
double dy = 0.0;
double dth = 0.0;
double dxy = 0.0;
double vx = 0.0;
double vy = 0.0;
// double vth = 0.0;
// ros::Duration d(0,1000000);
/*
double x = 1.0;
double y = 0.0;
double theta = 1.57;
*/

std_msgs::String str_msg;
std_msgs::Float32 f32_battery_voltage_msg;
std_msgs::Float32 f32_charge_voltage_msg;
std_msgs::Int16 int16_charge_pwm_msg;
std_msgs::Bool bool_blade_state_msg;
std_msgs::Bool bool_charging_state_msg;
nav_msgs::Odometry odom_msg;
std_msgs::UInt16 left_encoder_val_msg;
std_msgs::UInt16 right_encoder_val_msg;
std_msgs::Float32 tot_speed_val_msg;


/*
 * PUBLISHERS
 */
ros::Publisher chatter("mowgli/version", &str_msg);
ros::Publisher pubBatteryVoltage("mowgli/battery_voltage", &f32_battery_voltage_msg);
ros::Publisher pubChargeVoltage("mowgli/charge_voltage", &f32_charge_voltage_msg);
ros::Publisher pubChargePWM("mowgli/charge_pwm", &int16_charge_pwm_msg);
ros::Publisher pubChargeingState("mowgli/charging_state", &bool_charging_state_msg);
ros::Publisher pubBladeState("mowgli/blade_state", &bool_blade_state_msg);
ros::Publisher pubOdom("mowgli/odom", &odom_msg);
ros::Publisher pubLeftEncoderVal("mowgli/left_encoder_val", &left_encoder_val_msg);
ros::Publisher pubRightEncoderVal("mowgli/right_encoder_val", &right_encoder_val_msg);
ros::Publisher pubTotSpeedVal("mowgli/tot_speed_val", &tot_speed_val_msg);


/*
 * SUBSCRIBERS
 */
extern "C" void CommandVelocityMessageCb(const geometry_msgs::Twist& msg);
extern "C" void CommandBladeOnMessageCb(const std_msgs::Bool& msg);
extern "C" void CommandBladeOffMessageCb(const std_msgs::Bool& msg);

ros::Subscriber<geometry_msgs::Twist> subCommandVelocity("mowgli/cmd_vel", CommandVelocityMessageCb);
ros::Subscriber<std_msgs::Bool> subBladeOn("mowgli/cmd_blade_on", CommandBladeOnMessageCb);
ros::Subscriber<std_msgs::Bool> subBladeOff("mowgli/cmd_blade_off", CommandBladeOffMessageCb);
// TODO ros::Subscriber<std_msgs::Bool> subLEDSet("cmd_panel_led_set", CommandLEDSetMessageCb);
// TODO ros::Subscriber<std_msgs::Bool> subLEDFlashSlow("cmd_panel_led_flash_slow", CommandLEDFlashSlowMessageCb);
// TODO ros::Subscriber<std_msgs::Bool> subLEDFlashFast("cmd_panel_led_flash_fast", CommandLEDFlashFastMessageCb);
// TODO ros::Subscriber<std_msgs::Bool> subLEDClear("cmd_panel_led_clear", CommandLEDClearMessageCb);

/*
 * NON BLOCKING TIMERS
 */
static nbt_t ros_nbt;
static nbt_t publish_nbt;
static nbt_t motors_nbt;
static nbt_t panel_nbt;
static nbt_t broadcast_nbt;

/*
 * receive and parse cmd_blade_on messages
 * if True, turns ON the Blade Motor - (False is ignored, use the cmd_blade_off with a True message to turn it off)
 */
extern "C" void CommandBladeOnMessageCb(const std_msgs::Bool& msg)
{	
	debug_printf("/cmd_blade_on: %d\r\n", msg.data);
	if (msg.data)
	{
		blade_on_off = true;
	}
}

/*
 * receive and parse cmd_blade_on messages
 * if True, turns OFF the Blade Motor - (False is ignored, use the cmd_blade_on with a True message to turn it ON)
 */
extern "C" void CommandBladeOffMessageCb(const std_msgs::Bool& msg)
{	
	debug_printf("/cmd_blade_off: %d\r\n", msg.data);
	if (msg.data)
	{
		blade_on_off = false;
	}
}


/*
 * receive and parse cmd_vel messages
 * actual driving (updating drivemotors) is done in the drivemotors_nbt
 */
extern "C" void CommandVelocityMessageCb(const geometry_msgs::Twist& msg)
{
		//	debug_printf("x: %f  z: %f\r\n", msg.linear.x, msg.angular.z);

		// calculate twist speeds to add/substract 
		float left_twist_mps = -1.0 * msg.angular.z / 3.1416 * WHEEL_BASE;
		float right_twist_mps = msg.angular.z / 3.1416 * WHEEL_BASE;
    

		// add them to the linear speed 
		float left_mps = msg.linear.x + left_twist_mps;
        float right_mps = msg.linear.x + right_twist_mps;

		// cap left motor speed to MAX_MPS
		if (left_mps > MAX_MPS)
		{
			left_mps = MAX_MPS;
		}
		else if (left_mps < -1*MAX_MPS)
		{
			left_mps = -1*MAX_MPS;
		}
		// cap right motor speed to MAX_MPS
		if (right_mps > MAX_MPS)
		{
			right_mps = MAX_MPS;
		}
		else if (right_mps < -1*MAX_MPS)
		{
			right_mps = -1*MAX_MPS;
		}

		// set directions		
		left_dir = (left_mps >= 0)?1:0;
		right_dir = (right_mps >= 0)?1:0;

		// set drivemotors PWM values
		left_speed = abs(left_mps * PWM_PER_MPS);
		right_speed = abs(right_mps * PWM_PER_MPS);		

	//	debug_printf("left_mps: %f (%c)  right_mps: %f (%c)\r\n", left_mps, left_dir?'F':'R', right_mps, right_dir?'F':'R');
}

extern "C" void cdc_receive_put(uint8_t value)
{
	ringbuffer_putchar(&rb, value);
}


/*
 * Update various chatters topics
 */
extern "C" void chatter_handler()
{
	  if (NBT_handler(&publish_nbt))
	  {
		  /*
		  char version[] = "version: 0.1";
		  str_msg.data = version;
		  chatter.publish(&str_msg);
		  */
		  
		  f32_battery_voltage_msg.data = ADC_BatteryVoltage();
		  pubBatteryVoltage.publish(&f32_battery_voltage_msg);

		  f32_charge_voltage_msg.data = ADC_ChargeVoltage();
		  pubChargeVoltage.publish(&f32_charge_voltage_msg);

		  int16_charge_pwm_msg.data = chargecontrol_pwm_val;
		  pubChargePWM.publish(&int16_charge_pwm_msg);

		  bool_charging_state_msg.data =  chargecontrol_is_charging;
		  pubChargeingState.publish(&bool_charging_state_msg);
 		  bool_blade_state_msg.data = true; // TODO: read blade status
		  pubBladeState.publish(&bool_blade_state_msg);

		  HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_PIN);         // flash LED
	  }
}

/*
 *  Drive Motors handler
 *  Blade Motor handler
 */
extern "C" void motors_handler()
{
	  if (NBT_handler(&motors_nbt))
	  {	
		setDriveMotors(left_speed, right_speed, left_dir, right_dir);		
		setBladeMotor(blade_on_off);		

		// char setmotorsok[] = "Motors Set";
		// str_msg.data = setmotorsok;
		// chatter.publish(&str_msg);
	
	  }
}

/*
 *  Keyboard/LED Panel handler
 */
extern "C" void panel_handler()
{
	  if (NBT_handler(&panel_nbt))
	  {			  
		PANEL_Tick();		
	  }
}

extern "C" void broadcast_handler()
{
	  if (NBT_handler(&broadcast_nbt))
	  {		
			// z = BROADCAST_NBT_TIME_MS/1000;
			// x = right_wheel_speed_val;
			// y = left_wheel_speed_val;
			current_time = nh.now();
			//////////////////////////////////////////////////
			// TF message
			//////////////////////////////////////////////////
			speed_act_left = left_wheel_speed_val/PWM_PER_MPS;			// wheel speed in m/s
			speed_act_right = right_wheel_speed_val/PWM_PER_MPS;			// wheel speed in m/s
		//	debug_printf("speed_act_left: %f speed_act_right: %f\r\n",  speed_act_left, speed_act_right);		
			dt = (BROADCAST_NBT_TIME_MS/1000.0);
			dxy = (speed_act_left+speed_act_right)*dt/2.0;
			dth = ((speed_act_right-speed_act_left)*dt)/WHEEL_BASE;

		//	debug_printf("dt: %f dxy: %f dth: %f\r\n",  dt, dxy, dth);		

			if (dth > 0) dth *= angular_scale_positive;
			if (dth < 0) dth *= angular_scale_negative;
			if (dxy > 0) dxy *= linear_scale_positive;
			if (dxy < 0) dxy *= linear_scale_negative;

			dx = cos(dth) * dxy;
			dy = sin(dth) * dxy;

			x_pos += (cos(theta) * dx - sin(theta) * dy);
			y_pos += (sin(theta) * dx + cos(theta) * dy);
			theta += dth;

			if(theta >= two_pi) theta -= two_pi;
			if(theta <= -two_pi) theta += two_pi;

		  if (false) {
						quat = tf::createQuaternionFromYaw(theta);
						if(publish_tf) {
							geometry_msgs::TransformStamped t;						
							t.header.frame_id = odom;
							t.child_frame_id = base_link;
							t.transform.translation.x = x_pos;
							t.transform.translation.y = y_pos;
							t.transform.translation.z = 0.0;
							t.transform.rotation = quat;
							t.header.stamp = current_time;					
							broadcaster.sendTransform(t);			
						}
						//////////////////////////////////////////////////
						// odom message
						//////////////////////////////////////////////////
						odom_msg.header.stamp = current_time;
						odom_msg.header.frame_id = odom;
						odom_msg.pose.pose.position.x = x_pos;
						odom_msg.pose.pose.position.y = y_pos;
						odom_msg.pose.pose.position.z = 0.0;
						odom_msg.pose.pose.orientation = quat;
						if (speed_act_left == 0 && speed_act_right == 0){
							odom_msg.pose.covariance[0] = 1e-9;
							odom_msg.pose.covariance[7] = 1e-3;
							odom_msg.pose.covariance[8] = 1e-9;
							odom_msg.pose.covariance[14] = 1e6;
							odom_msg.pose.covariance[21] = 1e6;
							odom_msg.pose.covariance[28] = 1e6;
							odom_msg.pose.covariance[35] = 1e-9;
							odom_msg.twist.covariance[0] = 1e-9;
							odom_msg.twist.covariance[7] = 1e-3;
							odom_msg.twist.covariance[8] = 1e-9;
							odom_msg.twist.covariance[14] = 1e6;
							odom_msg.twist.covariance[21] = 1e6;
							odom_msg.twist.covariance[28] = 1e6;
							odom_msg.twist.covariance[35] = 1e-9;
						}
						else{
							odom_msg.pose.covariance[0] = 1e-3;
							odom_msg.pose.covariance[7] = 1e-3;
							odom_msg.pose.covariance[8] = 0.0;
							odom_msg.pose.covariance[14] = 1e6;
							odom_msg.pose.covariance[21] = 1e6;
							odom_msg.pose.covariance[28] = 1e6;
							odom_msg.pose.covariance[35] = 1e3;
							odom_msg.twist.covariance[0] = 1e-3;
							odom_msg.twist.covariance[7] = 1e-3;
							odom_msg.twist.covariance[8] = 0.0;
							odom_msg.twist.covariance[14] = 1e6;
							odom_msg.twist.covariance[21] = 1e6;
							odom_msg.twist.covariance[28] = 1e6;
							odom_msg.twist.covariance[35] = 1e3;
						}
						vx = (dt == 0)?  0 : (speed_act_left+speed_act_right)/2.0;
					//	vth = (dt == 0)? 0 : (speed_act_right-speed_act_left)/WHEEL_BASE;
						odom_msg.child_frame_id = base_link;
						odom_msg.twist.twist.linear.x = vx;
						odom_msg.twist.twist.linear.y = 0.0;
						odom_msg.twist.twist.angular.z = dth;
						// pubOdom.publish(&odom_msg);
		  }
		left_encoder_val_msg.data = left_encoder_val;
		pubLeftEncoderVal.publish(&left_encoder_val_msg);
		right_encoder_val_msg.data = right_encoder_val;
		pubRightEncoderVal.publish(&right_encoder_val_msg);
		double totSpeed = -0.5 * (speed_act_left + speed_act_right);
		tot_speed_val_msg.data = totSpeed;
		pubTotSpeedVal.publish(&tot_speed_val_msg);
/*
		double dx = 0.2;
		double dtheta = 0.18;

		x += cos(theta)*dx*0.1;
		y += sin(theta)*dx*0.1;
		theta += dtheta*0.1;
		if (theta > 3.14)
		  	theta = -3.14;
		t.header.frame_id = odom;
		t.child_frame_id = base_link;
		t.transform.translation.x = x;
		t.transform.translation.y = y;
		t.transform.rotation = tf::createQuaternionFromYaw(theta);
		t.header.stamp = nh.now();
		
		broadcaster.sendTransform(t);		
*/		


	  }
}

/*
 * ROS housekeeping
 */
extern "C" void spinOnce()
{
	  if (NBT_handler(&ros_nbt))
	  {
			nh.spinOnce();
	  }
}

/* 
 *  Initialize rosserial
 */
extern "C" void init_ROS()
{
	ringbuffer_init(&rb, RxBuffer, RxBufferSize);

	// Initialize ROS
	nh.initNode();

	// Initialize TF Broadcaster
	// broadcaster.init(nh);

	// Initialize Pubs
	nh.advertise(chatter);
	nh.advertise(pubBatteryVoltage);
	nh.advertise(pubChargeVoltage);
	nh.advertise(pubChargePWM);
	nh.advertise(pubOdom);
	nh.advertise(pubBladeState);
	nh.advertise(pubChargeingState);
	nh.advertise(pubLeftEncoderVal);
	nh.advertise(pubRightEncoderVal);
	nh.advertise(pubTotSpeedVal);
	
	// Initialize Subs
	nh.subscribe(subCommandVelocity);
	// nh.subscribe(subBladeOn);
	// nh.subscribe(subBladeOff);

	// Initialize Timers
	NBT_init(&publish_nbt, 1000);
	NBT_init(&panel_nbt, 100);
	NBT_init(&motors_nbt, BROADCAST_NBT_TIME_MS);
	NBT_init(&broadcast_nbt, BROADCAST_NBT_TIME_MS);
	NBT_init(&ros_nbt, 10);	
}