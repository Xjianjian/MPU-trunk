/**********************************************************************************
	
	用于远控预约功能定时唤醒MCU和MPU

*********************************************************************************/
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include  <errno.h>
#include <sys/times.h>
#include <sys/time.h>
#include "timer.h"
#include <sys/prctl.h>
#include "log.h"	
#include <sys/types.h>
#include <sysexits.h>	/* for EX_* exit codes */
#include <assert.h>	/* for assert(3) */
#include "constr_TYPE.h"
#include "asn_codecs.h"
#include "asn_application.h"
#include "asn_internal.h"	/* for _ASN_DEFAULT_STACK_MAX */
#include "scom_api.h"
#include "at.h"
#include "pm_api.h"
#include "scom_msg_def.h"
#include "../../../../base/scom/scom_tl.h"
#include "PP_ACCtrl.h"
#include "PP_ChargeCtrl.h"
#include "PP_SendWakeUptime.h"

static PP_wake_t wake_period[7] =
{
	{0,0x01},//星期7
	{1,0x40},//星期1
	{2,0x20},//星期2
	{3,0x10},//星期3
	{4,0x08},//星期4
	{5,0x04},//星期5
	{6,0x02},//星期6
};

int PP_waketime_to_min(waketime * pt)
{
	int k,i;
	int temp_min = 0;
	int low_min = 0;
	time_t timep;
	struct tm *localdatetime;
	time(&timep);  //获取从1970.1.1 00:00:00到现在的秒数
	localdatetime = localtime(&timep);//获取本地时间
	for( i =localdatetime->tm_wday ; i <=localdatetime->tm_wday+7 ;i++)
	{	
		if(i>=7)
		{
			k = i-7;
		}
		else
		{
			k = i;
		}
		if(wake_period[k].mask & pt->period)
		{
			//log_o(LOG_HOZON,"i = %d",i);
			//log_o(LOG_HOZON,"k = %d",k);
			if((pt->hour + (i - localdatetime->tm_wday) *24 ) < localdatetime->tm_hour)
			{
				continue;
			}
			else if((pt->hour + (i - localdatetime->tm_wday) *24 ) == localdatetime->tm_hour)
			{
				if(pt->min <= localdatetime->tm_min)
				{
					continue;
				}
				else
				{
					temp_min = pt->min - localdatetime->tm_min;
				}
			}
			else
			{
				if(pt->min < localdatetime->tm_min)
				{
					temp_min = (pt->hour + (i - localdatetime->tm_wday) *24- localdatetime->tm_hour - 1) * 60 + \
									pt->min + 60 - localdatetime->tm_min;
				}
				else
				{
					temp_min = (pt->hour + (i - localdatetime->tm_wday) *24 - localdatetime->tm_hour ) * 60 + \
									pt->min - localdatetime->tm_min;
				}
			}

			if(low_min == 0)
			{
				low_min = temp_min;
			}
			else
			{
				if(low_min > temp_min)
				{
					low_min = temp_min;
				}
			}	
		}
	}
	//log_o(LOG_HOZON,"low_min = %d",low_min);
	return low_min;
}

#if 0
int PP_waketime_to_min(waketime * pt)
{
	uint8_t wday;
	int temp_min = 0;
	int low_min = 0;
	time_t timep;
	struct tm *localdatetime;
	time(&timep);  //获取从1970.1.1 00:00:00到现在的秒数
	localdatetime = localtime(&timep);//获取本地时间

	for(wday = 0;wday < 7;wday++)//逐一检查周一到周天
	{
		if(wake_period[wday].mask & pt->period)//检查到当前星期有预约
		{
			if(wday < localdatetime->tm_wday)//下周的预约
			{
				temp_min = ;
			}
			else if(wday == localdatetime->tm_wday)//
			{
				if(localdatetime->tm_hour > pt->hour)//下周的预约
				{
					temp_min = ;
				}
				else if(localdatetime->tm_hour == pt->hour)
				{
					if(localdatetime->tm_min > pt->min)//下周的预约
					{
						temp_min = ;
					}
					else//本周的预约
					{
						temp_min = ;
					}
				}
				else//本周的预约
				{
					temp_min = ;
				}
			}
			else//本周的预约
			{
				temp_min = ;
			}
		}
	}

	if(low_min == 0)
	{
		low_min = temp_min;
	}
	else
	{
		if(low_min > temp_min)
		{
			low_min = temp_min;
		}
	}	

	return low_min;
}
#endif

int pp_get_low_waketime(void)
{
	int ac_time = 0 ;
	int charge_time = 0;
	int fota_time = 0;
	int low_time = 0;
	ac_time = PP_ACCtrl_waketime();
	charge_time = PP_ChargeCtrl_waketime();
	fota_time = 60;
	
	
	return low_time;
}


void PP_Send_WakeUpTime_to_Mcu(void)
{
	int low_min = 0;
	static int sleep_flag = 0;
	if(at_get_pm_mode() == PM_RUNNING_MODE)
	{
		sleep_flag = 0;
	}

	if(at_get_pm_mode() == PM_LISTEN_MODE)
	{	
		low_min = pp_get_low_waketime();
		if(sleep_flag == 0)
		{
			if(low_min != 0)
			{
				scom_tl_send_frame(SCOM_TL_CMD_WAKE_TIME, SCOM_TL_SINGLE_FRAME, 0,
                           		 (unsigned char *)&low_min, sizeof(low_min));
				sleep_flag = 1;
				log_o(LOG_HOZON,"low_time = %d\n",low_min);
			}
		}
	}
}
