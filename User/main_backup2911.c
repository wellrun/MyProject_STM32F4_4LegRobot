/*****************************	     	DAMH2		    	*****************************************************										
										           | |                     | |
											      RFL_2(6-PC6)     		    RHL_2(3-PB6)
														   | |                     | |
														RFL_1(8-PC7)     		    RHL_1(1-PB7)    
											 ____________________________________________
											|                                            |
											|	     RFL_0(7-PC8)	        	RHL_0(2-PB8)   |
											|		         ____________________            |
											|		        |										 | 	         |
											|		        |										 | 	         |
											|		        |										 | 	         |
											|		        |____________________|	         |
											|                                            |
											|     LFL_0(9-PA2)      	    LHL_0(15-PB11) |
											|____________________________________________|	
														LFL_1(10-PA1)      		  LHL_1(16-PB10)
														   | |                    | |
											    	LFL_2(11-PA0)      	    LHL_2(14-PB3)
														   | |                    | |

******************************************************************************************************
RC_position_Control
                         min_phi                       max_phi
degree                      0                             120
PWM_CCR_pulse              2710                           7390

angle_step = (7390-2710)/120 = 39
******************************************************************************************************
RC_Speed_Control
 
  w (degree/second) = SysTick_interval * 1/time *1/k * 120/(7390-2710)
							                           (systick_subroutine)
******************************************************************************************************
USART 
       TX:PD8										 			RX:PD9
******************************************************************************************************
I2C 
       PA8: I2C3_SCL                 PC9:I2C3_SDA
******************************************************************************************************
*note for programing: Dont use Delay in Systick functions, can cause other systick functions stop working
			                Bring the use-delay function to main so systick functions can work properly like interrupt
******************************************************************************************************/
#include "stm32f4xx.h"
#include "PWM.h"
#include "UART_DMA.h"
#include "I2C.h"

#include "kalman.h"
#include "fuzzy.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#define SysTick_interval 10000
float pi= 3.141592654;

//LFL
#define phi0_LFL_2 2944        //10
#define phi0_LFL_1 3880       //30
#define phi0_LFL_0 5050				//60

//LHL
#define phi0_LHL_2 7312				//110
#define phi0_LHL_1 6064				//90
#define phi0_LHL_0 5050				//60

//RFL
#define phi0_RFL_2 7000				//110
#define phi0_RFL_1 6220				//90
#define phi0_RFL_0 4738				//60

//RHL
#define phi0_RHL_2 3100				//10
#define phi0_RHL_1 4036				//30
#define phi0_RHL_0 5050				//60


#define angle_step 39

/************************Global variables ----------------------------------------------------------*/
//Systick section
uint16_t SysTicker = 0;
uint32_t CCR_now;
//UART_DMA section
	//Tx
	#define txsize 7
	uint8_t txbuffer[txsize];

	//RX
	#define rxsize 56 //min = 7 channel * 7 bytes = 49
	uint8_t rxbuffer[rxsize];
	char RX_tempbuffer;
	uint8_t rxindex=0,i;
	float   rx_data;
	int8_t	X1_Analog,
					X2_Analog,
					Y1_Analog,
					Y2_Analog,
					LT_Analog,
					RT_Analog;
	uint16_t button;

//I2C_MPU section
#define MPU_Adress 0x68

float gyroX;
float gyroY;
float gyroZ;
float accelX;
float accelY;
float accelZ;
float tempC;

float roll,pitch,yaw;
float kalAngleX;
float theta_r,theta_p,theta_y;
float theta_r_prev, theta_r_dot;
float theta_p_prev, theta_p_dot;
float theta_y_prev, theta_y_dot;
float temp;

uint8_t I2C_RxBuffer[14];
//Set angle
uint16_t phi_LFL_2,k_LFL_2,
				 phi_LFL_1,k_LFL_1,
				 phi_LFL_0,k_LFL_0;
uint16_t phi_LHL_2,k_LHL_2,
				 phi_LHL_1,k_LHL_1,
				 phi_LHL_0,k_LHL_0;
uint16_t phi_RFL_2,k_RFL_2,
				 phi_RFL_1,k_RFL_1,
				 phi_RFL_0,k_RFL_0;
uint16_t phi_RHL_2,k_RHL_2,
				 phi_RHL_1,k_RHL_1,
				 phi_RHL_0,k_RHL_0;

//Status feedback
int8_t   fb_LFL_2,fb_LFL_1,fb_LFL_0,
				 fb_LHL_2,fb_LHL_1,fb_LHL_0,
				 fb_RFL_2,fb_RFL_1,fb_RFL_0,
				 fb_RHL_2,fb_RHL_1,fb_RHL_0;

//Inverse Kinematics
#define a0    6
#define a1		6
#define a2		10
int8_t   theta_LFL_0,theta_LFL_1,theta_LFL_2,
				 theta_RFL_0,theta_RFL_1,theta_RFL_2,
				 theta_LHL_0,theta_LHL_1,theta_LHL_2,
				 theta_RHL_0,theta_RHL_1,theta_RHL_2;


unsigned int t=0;

//trotting elipse
float 
			trot_A_LFL_y = 2,trot_offset_LFL_y = -1,
			trot_A_LFL_z = 2,trot_offset_LFL_z = 13;

float 
			trot_A_RFL_y = 2,trot_offset_RFL_y = -1,
			trot_A_RFL_z = 2,trot_offset_RFL_z = 13;
			
float
			trot_A_LHL_y = 2,trot_offset_LHL_y = 2,
			trot_A_LHL_z = 2,trot_offset_LHL_z = 13;

float 
			trot_A_RHL_y = 2,trot_offset_RHL_y = 2,
			trot_A_RHL_z = 2,trot_offset_RHL_z = 13;

int8_t trot_z_LF,trot_y_LF,
			 trot_z_RF,trot_y_RF,
			 trot_z_LH,trot_y_LH,
			 trot_z_RH,trot_y_RH;
#define f 2 //(Hz)
#define sample_interval 1

////creep 
unsigned int t0,t1,t2,t3,t4;	
float creep_A_LFL_x = 4,creep_offset_LFL_x = 8,
			creep_A_LFL_y = 2,creep_offset_LFL_y = 3,
			creep_A_LFL_z = 3,creep_offset_LFL_z = 12;

float creep_A_RFL_x = 4,creep_offset_RFL_x = 8,
			creep_A_RFL_y = 2,creep_offset_RFL_y = 3,
			creep_A_RFL_z = 3,creep_offset_RFL_z = 12;
			
float creep_A_LHL_x = 4,creep_offset_LHL_x = 8,
			creep_A_LHL_y = 2,creep_offset_LHL_y = 3,
			creep_A_LHL_z = 3,creep_offset_LHL_z = 12;

float creep_A_RHL_x = 4,creep_offset_RHL_x = 8,
			creep_A_RHL_y = 2,creep_offset_RHL_y = 3,
			creep_A_RHL_z = 3,creep_offset_RHL_z = 12;
float creep_x_In = -20,creep_x_Out = 20;
		

int8_t creep_z_LF,creep_y_LF,creep_x_LF,
			 creep_z_RF,creep_y_RF,creep_x_RF,
			 creep_z_LH,creep_y_LH,creep_x_LH,
			 creep_z_RH,creep_y_RH,creep_x_RH;
#define tdelay1 1
#define tdelay2 10
#define tdelay3 100
		
/************************Subroutines_Prototype****************************/
void Delay(uint32_t nCount);// t = nCount (ms)
//void delay(uint16_t time); //delay func using systick
void Zero_state (void);
void X360_button (void);
	void action1 (void);
	void action2 (void);
	void action3 (void);
	void action4 (void);
void X360_Y1_Analog (void);
void X360_Y2_Analog (void);
		//gaits
		void trot (void);
		void creep (void);
//Inverse Kinematics 3DOF
void LFL_IK (float x,float y,float z);
void RFL_IK (float x,float y,float z);	
void LHL_IK (float x,float y,float z);	
void RHL_IK (float x,float y,float z);	

//Inverse Kinematics 2DOF
void LFL_IK_2DOF (float y,float z1);
void RFL_IK_2DOF (float y,float z1);	
void LHL_IK_2DOF (float y,float z1);	
void RHL_IK_2DOF (float y,float z1);

//Set
void LFL_2_set (int8_t phi, uint8_t k);
void LFL_1_set (int8_t phi, uint8_t k);
void LFL_0_set (int8_t phi, uint8_t k);

void LHL_2_set (int8_t phi, uint8_t k);
void LHL_1_set (int8_t phi, uint8_t k);
void LHL_0_set (int8_t phi, uint8_t k);

void RFL_2_set (int8_t phi, uint8_t k);
void RFL_1_set (int8_t phi, uint8_t k);
void RFL_0_set (int8_t phi, uint8_t k);

void RHL_2_set (int8_t phi, uint8_t k);
void RHL_1_set (int8_t phi, uint8_t k);
void RHL_0_set (int8_t phi, uint8_t k);

//Systick prototype
void feedback (uint16_t time);
void timer (uint16_t time);
void Read_MPU (uint16_t time);
void UART_TX (uint16_t time);
//Execute
void LFL_2_execute(uint16_t time);
void LFL_1_execute(uint16_t time);
void LFL_0_execute(uint16_t time);

void LHL_2_execute(uint16_t time);
void LHL_1_execute(uint16_t time);
void LHL_0_execute(uint16_t time);

void RFL_2_execute(uint16_t time);
void RFL_1_execute(uint16_t time);
void RFL_0_execute(uint16_t time);

void RHL_2_execute(uint16_t time);
void RHL_1_execute(uint16_t time);
void RHL_0_execute(uint16_t time);

/************************MAIN*********************************************/
int main(void)
{
	TIM_Config();
	PWM_Config();
	UART_DMA_CONFIG(txbuffer,txsize,rxbuffer,rxsize,57600);
	USART_config ();
	I2C_Config();
	Zero_state ();
	
	//Chuyen MPU6050 tu che do sleep sang wake
	MPU_Config(MPU_Adress);
	MPU_Wake(MPU_Adress);
	//----------------------------------------
	
	//Cai dat ma tran Q R cho Kalman filter
	Kalman();
	//--------------------------------------
	
	//Dat goc ban dau cho Kalman filter
	MPU_ReadAll(I2C_RxBuffer, MPU_Adress);
	MPU_GetRawData(I2C_RxBuffer, &accelX, &accelY, &accelZ, &tempC, &gyroX, &gyroY, &gyroZ);
	roll  = atan2f(accelY, accelZ) * 180/(pi);
	setAngle(roll);
	//-----------------------------------
	
	
	if (SysTick_Config(SystemCoreClock / SysTick_interval)) // systick_interrupt occur every 0.1 ms (1000=1ms)
  {/* Capture error */ while (1);}
	while (1)
	{
		//control signal
		X360_button ();
		X360_Y1_Analog();
		X360_Y2_Analog ();
	}
}

/************************Systick_interrupt_0.1ms******************************************/
void SysTick_Handler(void)
{
	SysTicker ++;
	//actuator
	LFL_2_execute(1);
	LFL_1_execute(1);
	LFL_0_execute(1);
	
	LHL_2_execute(1);
	LHL_1_execute(1);
	LHL_0_execute(1);
	
	RFL_2_execute(1);
	RFL_1_execute(1);
	RFL_0_execute(1);
	
	RHL_2_execute(1);
	RHL_1_execute(1);
	RHL_0_execute(1);
		
	//feedback
	feedback (10);
	//timer
	timer (10);
	//Read MPU data
	Read_MPU (200);
	//UART TX
  UART_TX (2000);
}

uint16_t GetTicker(void)
{return SysTicker;}

/************************Interrupts*****************************/
//UART RX 
void USART3_IRQHandler(void)
{
	if(USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
	{
			/* Read data from the receive data register */
			RX_tempbuffer = USART_ReceiveData(USART3);
			rxbuffer[rxindex] = RX_tempbuffer;
			rxindex++;
			if (rxindex == rxsize) rxindex=0;
			for (i=0;i<=rxsize-6;i++)
			{
				if ((rxbuffer[i] == 0xFF) && (rxbuffer[i + 6] == 0xFE))
				{
					memcpy(&rx_data,&rxbuffer[i+2],4);
					switch (rxbuffer[i+1])
					{
						case 0xE0:button = (uint16_t)rx_data;break;
						case 0xE1:X1_Analog = (int8_t)rx_data;break;
						case 0xE2:X2_Analog = (int8_t)rx_data;break;
						case 0xE3:Y1_Analog = (int8_t)rx_data;break;
						case 0xE4:Y2_Analog = (int8_t)rx_data;break;
						case 0xE5:LT_Analog = (int8_t)rx_data;break;
						case 0xE6:RT_Analog = (int8_t)rx_data;break;
					}
				}
			}
	}
}
/************************Subroutines****************************/
void Delay(uint32_t nCount)
{
  nCount = 42000 *nCount;
	while(nCount--);// tdelay (ms)= nCount [(4/168Mhz*1000)*nCount*42000]
}
//void delay(uint16_t time) //delay func using systick time 0.1ms
//{
//	uint16_t counter = GetTicker();
//	while ((uint16_t)(counter+time)!=GetTicker());// sticker go to 65355 then 0 then count up
//}
void Zero_state (void)
{
		LFL_2_set (0,2);
		LHL_2_set (0,2);
		RFL_2_set (0,2);
		RHL_2_set (0,2);

		LFL_1_set (0,2);
		LHL_1_set (0,2);
		RFL_1_set (0,2);
		RHL_1_set (0,2);
		
		LFL_0_set (0,2);
		LHL_0_set (0,2);
		RFL_0_set (0,2);
		RHL_0_set (0,2);
}
void X360_button (void)
{
		switch (button)
					{
						case 1:action1();break;
						case 2:action2();break;
						case 4:action3();break;
						case 8:action4();break;
					}
}
	void action1 (void) //button B
	{	
		LFL_IK (8,0,14);
		RFL_IK (8,0,14);
		LHL_IK (8,0,14);
		RHL_IK (8,0,14);
		Delay (500);
	}

	void action2 (void) //button A
	{
		
	}

	void action3 (void) // button X
	{

	}
	
	void action4 (void) // button Y
	{

	}
void X360_Y1_Analog (void)
{
	if (Y1_Analog > 10)
	{
		trot();
	}		
}

void X360_Y2_Analog (void)
{
	if (Y2_Analog > 10)
	{
		creep();
	}		
}
	void trot (void)
	{
		while (Y1_Analog > 10)
		{
			trot_y_LF =  	trot_offset_LFL_y + trot_A_LFL_y*cos (2*pi*f*t/1000);
			trot_z_LF =  	trot_offset_LFL_z + trot_A_LFL_z*sin (2*pi*f*t/1000);
			LFL_IK_2DOF (trot_y_LF,trot_z_LF);
			
			trot_y_RH =   trot_offset_RHL_y - trot_A_RHL_y*cos (2*pi*f*t/1000);
			trot_z_RH =   trot_offset_RHL_z + trot_A_RHL_z*sin (2*pi*f*t/1000);
			RHL_IK_2DOF (trot_y_RH,trot_z_RH);
			
			trot_y_RF =  	trot_offset_RFL_y + trot_A_RFL_y*cos (2*pi*f*t/1000 + pi);
			trot_z_RF =   trot_offset_RFL_z + trot_A_RFL_z*sin (2*pi*f*t/1000 + pi);
			RFL_IK_2DOF (trot_y_RF,trot_z_RF);
			
			trot_y_LH =   trot_offset_LHL_y - trot_A_LHL_y*cos (2*pi*f*t/1000 + pi);
			trot_z_LH =   trot_offset_LHL_z + trot_A_LHL_z*sin (2*pi*f*t/1000 + pi);
			LHL_IK_2DOF (trot_y_LH,trot_z_LH);
			Delay (10);
		}
}
			void creep (void)
			{
				while (Y2_Analog > 10)
				{                   
					t0 = t;
					t1 = t + 500;
					t2 = t + 1000;
					t3 = t + 1500;
					t4 = t + 2000;
					while ((t0 <= t)&&(t < t1))
					{
						//creep_x_LF = 	creep_offset_LFL_x - creep_A_LFL_x*sin(2*pi/4000*(t-t0));
						creep_y_LF = 	creep_offset_LFL_y + creep_A_LFL_y*cos(2*pi/3000*(t-t0));
						creep_z_LF = 	creep_offset_LFL_z + creep_A_LFL_z*sin(2*pi/3000*(t-t0));
						
						//creep_x_RF = 	creep_offset_RFL_x + creep_A_RFL_x*sin(2*pi/4000*(t-t0));
						creep_y_RF = 	creep_offset_RFL_y + creep_A_RFL_y*cos(2*pi/3000*(t-t2+1000));
						creep_z_RF = 	creep_offset_RFL_z + creep_A_RFL_z*sin(2*pi/3000*(t-t2+1000));
						
						//creep_x_LH = 	creep_offset_LHL_x - creep_A_LHL_x*sin(2*pi/4000*(t-t0));
						creep_y_LH =  creep_offset_LHL_y - creep_A_LHL_y*cos(2*pi/3000*(t-t3+500));
						creep_z_LH = 	creep_offset_LHL_z + creep_A_LHL_z*sin(2*pi/3000*(t-t3+500));
						
						//creep_x_RH = 	creep_offset_RHL_x + creep_A_RHL_x*sin(2*pi/4000*(t-t0));
						creep_y_RH =  creep_offset_RHL_y + creep_A_RHL_y*cos(2*pi/1000*(t-t0));
						creep_z_RH = 	creep_offset_RHL_z - creep_A_RHL_z*sin(2*pi/1000*(t-t0));
						
						LFL_0_set (creep_x_In,2);
						LHL_0_set	(creep_x_In,2);
						RFL_0_set (creep_x_Out,2);
						RHL_0_set	(creep_x_Out,2);
						Delay (tdelay3);
						
						LFL_IK_2DOF (creep_y_LF,creep_z_LF);
						RFL_IK_2DOF (creep_y_RF,creep_z_RF);
						LHL_IK_2DOF (creep_y_LH,creep_z_LH);
						RHL_IK_2DOF (creep_y_RH,creep_z_RH);
						Delay (tdelay1);
					}
					
					while ((t1 <= t)&&(t < t2))
					{
						//creep_x_LF = 	creep_offset_LFL_x - creep_A_LFL_x*sin(2*pi/4000*(t-t0));
						creep_y_LF = 	creep_offset_LFL_y + creep_A_LFL_y*cos(2*pi/3000*(t-t0));
						creep_z_LF = 	creep_offset_LFL_z + creep_A_LFL_z*sin(2*pi/3000*(t-t0));
						
						//creep_x_RF = 	creep_offset_RFL_x + creep_A_RFL_x*sin(2*pi/4000*(t-t0));
						creep_y_RF = 	creep_offset_RFL_y - creep_A_RFL_y*cos(2*pi/1000*(t-t1));
						creep_z_RF = 	creep_offset_RFL_z - creep_A_RFL_z*sin(2*pi/1000*(t-t1));
						
						//creep_x_LH = 	creep_offset_LHL_x - creep_A_LHL_x*sin(2*pi/4000*(t-t0));
						creep_y_LH =  creep_offset_LHL_y - creep_A_LHL_y*cos(2*pi/3000*(t-t3+500));
						creep_z_LH = 	creep_offset_LHL_z + creep_A_LHL_z*sin(2*pi/3000*(t-t3+500));
						
						//creep_x_RH = 	creep_offset_RHL_x + creep_A_RHL_x*sin(2*pi/4000*(t-t0));
						creep_y_RH =  creep_offset_RHL_y - creep_A_RHL_y*cos(2*pi/3000*(t-t1));
						creep_z_RH = 	creep_offset_RHL_z + creep_A_RHL_z*sin(2*pi/3000*(t-t1));
						
						LFL_0_set (creep_x_In,2);
						LHL_0_set	(creep_x_In,2);
						RFL_0_set (creep_x_Out,2);
						RHL_0_set	(creep_x_Out,2);
						Delay (tdelay3);
						
						LFL_IK_2DOF (creep_y_LF,creep_z_LF);
						RFL_IK_2DOF (creep_y_RF,creep_z_RF);
						LHL_IK_2DOF (creep_y_LH,creep_z_LH);
						RHL_IK_2DOF (creep_y_RH,creep_z_RH);
						Delay (tdelay1);
					}
					
					while ((t2 <= t)&&(t < t3))
					{
						//creep_x_LF = 	creep_offset_LFL_x - creep_A_LFL_x*sin(2*pi/4000*(t-t0));
						creep_y_LF = 	creep_offset_LFL_y + creep_A_LFL_y*cos(2*pi/3000*(t-t0));
						creep_z_LF = 	creep_offset_LFL_z + creep_A_LFL_z*sin(2*pi/3000*(t-t0));
						
						//creep_x_RF = 	creep_offset_RFL_x + creep_A_RFL_x*sin(2*pi/4000*(t-t0));
						creep_y_RF = 	creep_offset_RFL_y + creep_A_RFL_y*cos(2*pi/3000*(t-t2));
						creep_z_RF = 	creep_offset_RFL_z + creep_A_RFL_z*sin(2*pi/3000*(t-t2));
						
						//creep_x_LH = 	creep_offset_LHL_x - creep_A_LHL_x*sin(2*pi/4000*(t-t0));
						creep_y_LH =  creep_offset_LHL_y + creep_A_LHL_y*cos(2*pi/1000*(t-t2));
						creep_z_LH = 	creep_offset_LHL_z - creep_A_LHL_z*sin(2*pi/1000*(t-t2));
						
						//creep_x_RH = 	creep_offset_RHL_x + creep_A_RHL_x*sin(2*pi/4000*(t-t0));
						creep_y_RH =  creep_offset_RHL_y - creep_A_RHL_y*cos(2*pi/3000*(t-t1));
						creep_z_RH = 	creep_offset_RHL_z + creep_A_RHL_z*sin(2*pi/3000*(t-t1));
						
						LFL_0_set (creep_x_Out,2);
						LHL_0_set	(creep_x_Out,2);
						RFL_0_set (creep_x_In,2);
						RHL_0_set	(creep_x_In,2);
						Delay (tdelay3);
						
						LFL_IK_2DOF (creep_y_LF,creep_z_LF);
						RFL_IK_2DOF (creep_y_RF,creep_z_RF);
						LHL_IK_2DOF (creep_y_LH,creep_z_LH);
						RHL_IK_2DOF (creep_y_RH,creep_z_RH);
						Delay (tdelay1);
					}
					
					while ((t3 <= t)&&(t < t4))
					{
						//creep_x_LF = 	creep_offset_LFL_x - creep_A_LFL_x*sin(2*pi/4000*(t-t0));
						creep_y_LF = 	creep_offset_LFL_y - creep_A_LFL_y*cos(2*pi/1000*(t-t3));
						creep_z_LF = 	creep_offset_LFL_z - creep_A_LFL_z*sin(2*pi/1000*(t-t3));
						
						//creep_x_RF = 	creep_offset_RFL_x + creep_A_RFL_x*sin(2*pi/4000*(t-t0));
						creep_y_RF = 	creep_offset_RFL_y + creep_A_RFL_y*cos(2*pi/3000*(t-t2));
						creep_z_RF = 	creep_offset_RFL_z + creep_A_RFL_z*sin(2*pi/3000*(t-t2));
						
						//creep_x_LH = 	creep_offset_LHL_x - creep_A_LHL_x*sin(2*pi/4000*(t-t0));
						creep_y_LH =  creep_offset_LHL_y - creep_A_LHL_y*cos(2*pi/3000*(t-t3));
						creep_z_LH = 	creep_offset_LHL_z + creep_A_LHL_z*sin(2*pi/3000*(t-t3));
						
						//creep_x_RH = 	creep_offset_RHL_x + creep_A_RHL_x*sin(2*pi/4000*(t-t0));
						creep_y_RH =  creep_offset_RHL_y - creep_A_RHL_y*cos(2*pi/3000*(t-t1));
						creep_z_RH = 	creep_offset_RHL_z + creep_A_RHL_z*sin(2*pi/3000*(t-t1));
						
						LFL_0_set (creep_x_Out,2);
						LHL_0_set	(creep_x_Out,2);
						RFL_0_set (creep_x_In,2);
						RHL_0_set	(creep_x_In,2);
						Delay (tdelay3);
						
						LFL_IK_2DOF (creep_y_LF,creep_z_LF);
						RFL_IK_2DOF (creep_y_RF,creep_z_RF);
						LHL_IK_2DOF (creep_y_LH,creep_z_LH);
						RHL_IK_2DOF (creep_y_RH,creep_z_RH);
						Delay (tdelay1);
					}
					Delay (tdelay2);
				}	
			}

// Systick subroutines
void feedback (uint16_t time)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		fb_LFL_2 = +((int16_t)TIM_GetCapture1(TIM5)-phi0_LFL_2)/angle_step;
		fb_LFL_1 = -((int16_t)TIM_GetCapture2(TIM5)-phi0_LFL_1)/angle_step;
		fb_LFL_0 = -((int16_t)TIM_GetCapture3(TIM5)-phi0_LFL_0)/angle_step;
		
		fb_LHL_2 = -((int16_t)TIM_GetCapture2(TIM2)-phi0_LHL_2)/angle_step;
		fb_LHL_1 = +((int16_t)TIM_GetCapture3(TIM2)-phi0_LHL_1)/angle_step;
		fb_LHL_0 = +((int16_t)TIM_GetCapture4(TIM2)-phi0_LHL_0)/angle_step;
		
		fb_RFL_2 = -((int16_t)TIM_GetCapture1(TIM3)-phi0_RFL_2)/angle_step;
		fb_RFL_1 = +((int16_t)TIM_GetCapture2(TIM3)-phi0_RFL_1)/angle_step;
		fb_RFL_0 = +((int16_t)TIM_GetCapture3(TIM3)-phi0_RFL_0)/angle_step;
		
		fb_RHL_2 = +((int16_t)TIM_GetCapture1(TIM4)-phi0_RHL_2)/angle_step;
		fb_RHL_1 = -((int16_t)TIM_GetCapture2(TIM4)-phi0_RHL_1)/angle_step;
		fb_RHL_0 = -((int16_t)TIM_GetCapture3(TIM4)-phi0_RHL_0)/angle_step;
	}
}
//timer
void timer (uint16_t time)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		t=t+1;                               //t = 1ms
	}
}

void Read_MPU (uint16_t time)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		theta_r_prev = theta_r;
		theta_p_prev = theta_p;
		theta_y_prev = theta_y;
		
		//Doc MPU6050 -- I2C
		MPU_ReadAll(I2C_RxBuffer, MPU_Adress);
		MPU_GetRawData(I2C_RxBuffer, &accelX, &accelY, &accelZ, &tempC, &gyroX, &gyroY, &gyroZ);
		
		//Kalman filter
		roll  = atan2f(accelY, accelZ) * 180/(pi);
	
		if ((roll < -90 && kalAngleX > 90) || (roll > 90 && kalAngleX < -90)) 
		{
			setAngle(roll);
			kalAngleX = roll;
		} 
		else
		{
			kalAngleX = getAngle(roll, gyroX, 0.01); // Calculate the angle using a Kalman filter
		}
		//Lam tron goc theta
		theta_r = kalAngleX*10;
		theta_r = floor(theta_r)/10.0;
		//-------------------------------
		
		//Tinh theta_dot
		if((100*abs(theta_r-theta_r_prev))>1000)
			theta_r_dot = 1;
		else
			theta_r_dot = 100*(theta_r - theta_r_prev)/500;
		//------------------------------------
	}
}
void UART_TX (uint16_t time)
{
	static uint16_t ticker=0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		SENDDATA(theta_r,txbuffer, 0);
	}
}
						/************************ Inverse Kinematics ****************************/
void LFL_IK (float x,float y,float z)
{
	float z1,
				theta_0_temp,theta_1_temp,theta_2_temp;
	
	z1 = sqrt(x*x + z*z - a0*a0);
	theta_0_temp = atan (z1/a0) + atan (x/z) - pi/2;
	theta_2_temp = acos (((y*y + z*z + x*x) - (a1*a1 + a2*a2 + a0*a0))/(2*a1*a2));
	theta_1_temp = atan (((a1+a2*cos(theta_2_temp))*y - a2* sin(theta_2_temp)*z1)/((a1+a2*cos(theta_2_temp))*z1 + a2* sin(theta_2_temp)*y));

	theta_LFL_0 =  (int8_t)(theta_0_temp*180/pi + 1/2);
	theta_LFL_1 =  (int8_t)(theta_1_temp*180/pi + 1/2);
	theta_LFL_2 =  (int8_t)(theta_2_temp*180/pi + 1/2);
	
	
	if (theta_LFL_0 < -40)  theta_LFL_0 = -40;
	if (theta_LFL_0 >  40)  theta_LFL_0 =  40;
	if (theta_LFL_1 < -80)  theta_LFL_0 = -80;
	if (theta_LFL_1 >  30)  theta_LFL_0 =  30;
	if (theta_LFL_2 <   0)  theta_LFL_0 =   0;
	if (theta_LFL_2 >  110) theta_LFL_0 = 110;
	
	LFL_0_set (theta_LFL_0,1);
	LFL_1_set (theta_LFL_1,1);
	LFL_2_set (theta_LFL_2,1);
}

void RFL_IK (float x,float y,float z)
{
	float z1,
				theta_0_temp,theta_1_temp,theta_2_temp;
	
	z1 = sqrt(x*x + z*z - a0*a0);
	theta_0_temp = atan (z1/a0) + atan (x/z) - pi/2;
	theta_2_temp = acos (((y*y + z*z + x*x) - (a1*a1 + a2*a2 + a0*a0))/(2*a1*a2));
	theta_1_temp = atan (((a1+a2*cos(theta_2_temp))*y - a2* sin(theta_2_temp)*z1)/((a1+a2*cos(theta_2_temp))*z1 + a2* sin(theta_2_temp)*y));

	theta_RFL_0 =  (int8_t)(theta_0_temp*180/pi + 1/2);
	theta_RFL_1 =  (int8_t)(theta_1_temp*180/pi + 1/2);
	theta_RFL_2 =  (int8_t)(theta_2_temp*180/pi + 1/2);
	
	
	if (theta_RFL_0 < -40)  theta_RFL_0 = -40;
	if (theta_RFL_0 >  40)  theta_RFL_0 =  40;
	if (theta_RFL_1 < -80)  theta_RFL_0 = -80;
	if (theta_RFL_1 >  30)  theta_RFL_0 =  30;
	if (theta_RFL_2 <   0)  theta_RFL_0 =   0;
	if (theta_RFL_2 >  110) theta_RFL_0 = 110;
	
	RFL_0_set (theta_RFL_0,1);
	RFL_1_set (theta_RFL_1,1);
	RFL_2_set (theta_RFL_2,1);
}

void LHL_IK (float x,float y,float z)
{
	float z1,
				theta_0_temp,theta_1_temp,theta_2_temp;
	
	z1 = sqrt(x*x + z*z - a0*a0);
	theta_0_temp = atan (z1/a0) + atan (x/z) - pi/2;
	theta_2_temp = acos (((y*y + z*z + x*x) - (a1*a1 + a2*a2 + a0*a0))/(2*a1*a2));
	theta_1_temp = atan (((a1+a2*cos(theta_2_temp))*y - a2* sin(theta_2_temp)*z1)/((a1+a2*cos(theta_2_temp))*z1 + a2* sin(theta_2_temp)*y));

	theta_LHL_0 =  (int8_t)(theta_0_temp*180/pi + 1/2);
	theta_LHL_1 =  (int8_t)(theta_1_temp*180/pi + 1/2);
	theta_LHL_2 =  (int8_t)(theta_2_temp*180/pi + 1/2);
	
	
	if (theta_LHL_0 < -40)  theta_LHL_0 = -40;
	if (theta_LHL_0 >  40)  theta_LHL_0 =  40;
	if (theta_LHL_1 < -80)  theta_LHL_0 = -80;
	if (theta_LHL_1 >  30)  theta_LHL_0 =  30;
	if (theta_LHL_2 <   0)  theta_LHL_0 =   0;
	if (theta_LHL_2 >  110) theta_LHL_0 = 110;
	
	LHL_0_set (theta_LHL_0,1);
	LHL_1_set (theta_LHL_1,1);
	LHL_2_set (theta_LHL_2,1);
}

void RHL_IK (float x,float y,float z)
{
	float z1,
				theta_0_temp,theta_1_temp,theta_2_temp;
	
	z1 = sqrt(x*x + z*z - a0*a0);
	theta_0_temp = atan (z1/a0) + atan (x/z) - pi/2;
	theta_2_temp = acos (((y*y + z*z + x*x) - (a1*a1 + a2*a2 + a0*a0))/(2*a1*a2));
	theta_1_temp = atan (((a1+a2*cos(theta_2_temp))*y - a2* sin(theta_2_temp)*z1)/((a1+a2*cos(theta_2_temp))*z1 + a2* sin(theta_2_temp)*y));

	theta_RHL_0 =  (int8_t)(theta_0_temp*180/pi + 1/2);
	theta_RHL_1 =  (int8_t)(theta_1_temp*180/pi + 1/2);
	theta_RHL_2 =  (int8_t)(theta_2_temp*180/pi + 1/2);
	
	
	if (theta_RHL_0 < -40)  theta_RHL_0 = -40;
	if (theta_RHL_0 >  40)  theta_RHL_0 =  40;
	if (theta_RHL_1 < -80)  theta_RHL_0 = -80;
	if (theta_RHL_1 >  30)  theta_RHL_0 =  30;
	if (theta_RHL_2 <   0)  theta_RHL_0 =   0;
	if (theta_RHL_2 >  110) theta_RHL_0 = 110;
	
	RHL_0_set (theta_RHL_0,1);
	RHL_1_set (theta_RHL_1,1);
	RHL_2_set (theta_RHL_2,1);
}
/// 2DOF Inverse kinematics
void LFL_IK_2DOF (float y,float z1)
{
	float theta_1_temp,theta_2_temp;
	
	theta_2_temp = acos (((y*y + z1*z1) - (a1*a1 + a2*a2))/(2*a1*a2));
	theta_1_temp = atan (((a1+a2*cos(theta_2_temp))*y - a2* sin(theta_2_temp)*z1)/((a1+a2*cos(theta_2_temp))*z1 + a2* sin(theta_2_temp)*y));

	theta_LFL_1 =  (int8_t)(theta_1_temp*180/pi + 1/2);
	theta_LFL_2 =  (int8_t)(theta_2_temp*180/pi + 1/2);
	
	if (theta_LFL_1 < -80)  theta_LFL_0 = -80;
	if (theta_LFL_1 >  30)  theta_LFL_0 =  30;
	if (theta_LFL_2 <   0)  theta_LFL_0 =   0;
	if (theta_LFL_2 >  110) theta_LFL_0 = 110;
	
	LFL_1_set (theta_LFL_1,1);
	LFL_2_set (theta_LFL_2,1);
}

void RFL_IK_2DOF (float y,float z1)
{
	float theta_1_temp,theta_2_temp;
	
	theta_2_temp = acos (((y*y + z1*z1) - (a1*a1 + a2*a2))/(2*a1*a2));
	theta_1_temp = atan (((a1+a2*cos(theta_2_temp))*y - a2* sin(theta_2_temp)*z1)/((a1+a2*cos(theta_2_temp))*z1 + a2* sin(theta_2_temp)*y));

	theta_RFL_1 =  (int8_t)(theta_1_temp*180/pi + 1/2);
	theta_RFL_2 =  (int8_t)(theta_2_temp*180/pi + 1/2);
	
	if (theta_RFL_1 < -80)  theta_RFL_0 = -80;
	if (theta_RFL_1 >  30)  theta_RFL_0 =  30;
	if (theta_RFL_2 <   0)  theta_RFL_0 =   0;
	if (theta_RFL_2 >  110) theta_RFL_0 = 110;
	
	RFL_1_set (theta_RFL_1,1);
	RFL_2_set (theta_RFL_2,1);
}

void LHL_IK_2DOF (float y,float z1)
{
	float theta_1_temp,theta_2_temp;
	
	theta_2_temp = acos (((y*y + z1*z1) - (a1*a1 + a2*a2))/(2*a1*a2));
	theta_1_temp = atan (((a1+a2*cos(theta_2_temp))*y - a2* sin(theta_2_temp)*z1)/((a1+a2*cos(theta_2_temp))*z1 + a2* sin(theta_2_temp)*y));

	theta_LHL_1 =  (int8_t)(theta_1_temp*180/pi + 1/2);
	theta_LHL_2 =  (int8_t)(theta_2_temp*180/pi + 1/2);
	
	if (theta_LHL_1 < -80)  theta_LHL_0 = -80;
	if (theta_LHL_1 >  30)  theta_LHL_0 =  30;
	if (theta_LHL_2 <   0)  theta_LHL_0 =   0;
	if (theta_LHL_2 >  110) theta_LHL_0 = 110;
	
	LHL_1_set (theta_LHL_1,1);
	LHL_2_set (theta_LHL_2,1);
}

void RHL_IK_2DOF (float y,float z1)
{
	float theta_1_temp,theta_2_temp;
	
	theta_2_temp = acos (((y*y + z1*z1) - (a1*a1 + a2*a2))/(2*a1*a2));
	theta_1_temp = atan (((a1+a2*cos(theta_2_temp))*y - a2* sin(theta_2_temp)*z1)/((a1+a2*cos(theta_2_temp))*z1 + a2* sin(theta_2_temp)*y));

	theta_RHL_1 =  (int8_t)(theta_1_temp*180/pi + 1/2);
	theta_RHL_2 =  (int8_t)(theta_2_temp*180/pi + 1/2);
	
	if (theta_RHL_1 < -80)  theta_RHL_0 = -80;
	if (theta_RHL_1 >  30)  theta_RHL_0 =  30;
	if (theta_RHL_2 <   0)  theta_RHL_0 =   0;
	if (theta_RHL_2 >  110) theta_RHL_0 = 110;
	
	RHL_1_set (theta_RHL_1,1);
	RHL_2_set (theta_RHL_2,1);
}
           /************************ Set ****************************/
void LFL_2_set (int8_t phi, uint8_t k) //phi = angle (degree)
{
	phi_LFL_2= phi0_LFL_2 +phi*angle_step;
	k_LFL_2=k;
}
void LFL_1_set (int8_t phi, uint8_t k) 
{
	phi_LFL_1= phi0_LFL_1 -phi*angle_step;
	k_LFL_1=k;
}
void LFL_0_set (int8_t phi, uint8_t k) 
{
	phi_LFL_0= phi0_LFL_0 -phi*angle_step;
	k_LFL_0=k;
}

void LHL_2_set (int8_t phi, uint8_t k) //phi = angle (degree)
{
	phi_LHL_2= phi0_LHL_2 -phi*angle_step;
	k_LHL_2=k;
}
void LHL_1_set (int8_t phi, uint8_t k) 
{
	phi_LHL_1= phi0_LHL_1 +phi*angle_step;
	k_LHL_1=k;
}
void LHL_0_set (int8_t phi, uint8_t k) 
{
	phi_LHL_0= phi0_LHL_0 +phi*angle_step;
	k_LHL_0=k;
}

void RFL_2_set (int8_t phi, uint8_t k) //phi = angle (degree)
{
	phi_RFL_2= phi0_RFL_2 -phi*angle_step;
	k_RFL_2=k;
}
void RFL_1_set (int8_t phi, uint8_t k) 
{
	phi_RFL_1= phi0_RFL_1 +phi*angle_step;
	k_RFL_1=k;
}
void RFL_0_set (int8_t phi, uint8_t k) 
{
	phi_RFL_0= phi0_RFL_0 +phi*angle_step;
	k_RFL_0=k;
}

void RHL_2_set (int8_t phi, uint8_t k) //phi = angle (degree)
{
	phi_RHL_2= phi0_RHL_2 +phi*angle_step;
	k_RHL_2=k;
}
void RHL_1_set (int8_t phi, uint8_t k) 
{
	phi_RHL_1= phi0_RHL_1 -phi*angle_step;
	k_RHL_1=k;
}
void RHL_0_set (int8_t phi, uint8_t k) 
{
	phi_RHL_0= phi0_RHL_0 -phi*angle_step;
	k_RHL_0=k;
}

					/************************ Execute - Systicks ****************************/
//LFL
void LFL_2_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture1(TIM5);
		if (phi_LFL_2>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_LFL_2) ==0)
				TIM_SetCompare1(TIM5, CCR_now);
		}
		else if (phi_LFL_2<CCR_now)
		{
			CCR_now--;
			if ((SysTicker % k_LFL_2) ==0)
				TIM_SetCompare1(TIM5, CCR_now);
		}
	}
}
void LFL_1_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture2(TIM5);
		if (phi_LFL_1>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_LFL_1) ==0)
				TIM_SetCompare2(TIM5, CCR_now);
		}
		else if (phi_LFL_1<CCR_now)
		{
			CCR_now--;
			if ((SysTicker % k_LFL_1) ==0)
				TIM_SetCompare2(TIM5, CCR_now);
		}
	}
}
void LFL_0_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture3(TIM5);
		if (phi_LFL_0>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_LFL_0) ==0)
				TIM_SetCompare3(TIM5, CCR_now);
		}
		else if (phi_LFL_0<CCR_now)
		{
			CCR_now--;
			if ((SysTicker % k_LFL_0) ==0)
				TIM_SetCompare3(TIM5, CCR_now);
		}
	}
}

//LHL
void LHL_2_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture2(TIM2);
		if (phi_LHL_2>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_LHL_2) ==0)
				TIM_SetCompare2(TIM2, CCR_now);
		}
		else if (phi_LHL_2<CCR_now)
		{
			CCR_now--;
			if ((SysTicker % k_LHL_2) ==0)
				TIM_SetCompare2(TIM2, CCR_now);
		}
	}
}
void LHL_1_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture3(TIM2);
		if (phi_LHL_1>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_LHL_1) ==0)
				TIM_SetCompare3(TIM2, CCR_now);
		}
		else if (phi_LHL_1<CCR_now)
		{
			CCR_now--;
			if ((SysTicker % k_LHL_1) ==0)
				TIM_SetCompare3(TIM2, CCR_now);
		}
	}
}
void LHL_0_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture4(TIM2);
		if (phi_LHL_0>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_LHL_0) ==0)
				TIM_SetCompare4(TIM2, CCR_now);
		}
		else if (phi_LHL_0<CCR_now)
		{
			CCR_now--;
			if ((SysTicker % k_LHL_0) ==0)
				TIM_SetCompare4(TIM2, CCR_now);
		}
	}
}

//RFL
void RFL_2_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture1(TIM3);
		if (phi_RFL_2>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_RFL_2) ==0)
				TIM_SetCompare1(TIM3, CCR_now);
		}
		else if (phi_RFL_2<CCR_now)
		{
			CCR_now--;
			if ((SysTicker % k_RFL_2) ==0)
				TIM_SetCompare1(TIM3, CCR_now);
		}
	}
}
void RFL_1_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture2(TIM3);
		if (phi_RFL_1>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_RFL_1) ==0)
				TIM_SetCompare2(TIM3, CCR_now);
		}
		else if (phi_RFL_1<CCR_now)
		{
			CCR_now--;
			if ((SysTicker % k_RFL_1) ==0)
				TIM_SetCompare2(TIM3, CCR_now);
		}
	}
}
void RFL_0_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture3(TIM3);
		if (phi_RFL_0>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_RFL_0) ==0)
				TIM_SetCompare3(TIM3, CCR_now);
		}
		else if (phi_RFL_0<CCR_now)
		{
			CCR_now--;
			if ((SysTicker % k_RFL_0) ==0)
				TIM_SetCompare3(TIM3, CCR_now);
		}
	}
}

//RHL
void RHL_2_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture1(TIM4);
		if (phi_RHL_2>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_RHL_2) ==0)
				TIM_SetCompare1(TIM4, CCR_now);
		}
		else if (phi_RHL_2<CCR_now)
		{
			CCR_now--;
			if ((SysTicker % k_RHL_2) ==0)
				TIM_SetCompare1(TIM4, CCR_now);
		}
	}
}
void RHL_1_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture2(TIM4);
		if (phi_RHL_1>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_RHL_1) ==0)
				TIM_SetCompare2(TIM4, CCR_now);
		}
		else if (phi_RHL_1<CCR_now)
		{
			CCR_now--;
			if ((SysTicker % k_RHL_1) ==0)
				TIM_SetCompare2(TIM4, CCR_now);
		}
	}
}
void RHL_0_execute(uint16_t time) // time = time*0.1 (ms)
{
	static uint16_t ticker = 0;
	if ((uint16_t)(ticker+time)==GetTicker()) 
	{
		ticker = GetTicker();
		
		CCR_now=TIM_GetCapture3(TIM4);
		if (phi_RHL_0>CCR_now)
		{
			CCR_now++;
			if ((SysTicker % k_RHL_0) ==0)
				TIM_SetCompare3(TIM4, CCR_now);
		}
		else if (phi_RHL_0<CCR_now)
		{
			CCR_now--;                  
			if ((SysTicker % k_RHL_0) ==0)
				TIM_SetCompare3(TIM4, CCR_now);
		}
	}
}
