/******************************************************
�ļ�����	PrvtProt_VehiSt.c

������	��ҵ˽��Э�飨�㽭���ڣ�	
Data			Vasion			author
2018/1/10		V1.0			liujian
*******************************************************/

/*******************************************************
description�� include the header file
*******************************************************/
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

#include <sys/types.h>
#include <sysexits.h>	/* for EX_* exit codes */
#include <assert.h>	/* for assert(3) */
#include "constr_TYPE.h"
#include "asn_codecs.h"
#include "asn_application.h"
#include "asn_internal.h"	/* for _ASN_DEFAULT_STACK_MAX */
#include "XcallReqinfo.h"
#include "Bodyinfo.h"
#include "per_encoder.h"
#include "per_decoder.h"

#include "init.h"
#include "log.h"
#include "list.h"
#include "../../support/protocol.h"
#include "../sockproxy/sockproxy_txdata.h"
#include "gb32960_api.h"
#include "hozon_SP_api.h"
#include "hozon_PP_api.h"
#include "shell_api.h"
#include "PrvtProt_shell.h"
#include "PrvtProt_queue.h"
#include "PrvtProt_EcDc.h"
#include "PrvtProt_cfg.h"
#include "PrvtProt.h"
#include "remoteControl/PPrmtCtrl_cfg.h"
#include "PrvtProt_SigParse.h"
#include "PrvtProt_VehiSt.h"

/*******************************************************
description�� global variable definitions
*******************************************************/

/*******************************************************
description�� static variable definitions
*******************************************************/
typedef struct
{
	PrvtProt_pack_Header_t	Header;
	PrvtProt_DisptrBody_t	DisBody;
}__attribute__((packed))  PrvtProt_VS_pack_t; /**/

typedef struct
{
	PrvtProt_VS_pack_t 		pack;
	PrvtProt_VSSt_t	 		state;
}__attribute__((packed))  PrvtProt_VS_t; /*�ṹ��*/

static PrvtProt_pack_t 		PP_VS_Pack;
static PrvtProt_VS_t		PP_rmtVS;
static PrvtProt_App_VS_t	PP_VS_appdata;

//static PrvtProt_TxInform_t VS_TxInform;
/*******************************************************
description�� function declaration
*******************************************************/
/*Global function declaration*/

/*Static function declaration*/
static int PP_VS_do_checksock(PrvtProt_task_t *task);
static int PP_VS_do_rcvMsg(PrvtProt_task_t *task);
static void PP_VS_RxMsgHandle(PrvtProt_task_t *task,PrvtProt_pack_t* rxPack,int len);
static int PP_VS_do_wait(PrvtProt_task_t *task);
static int PP_VS_do_VehiStMainfunction(PrvtProt_task_t *task);
static int PP_VS_VehiStatusResp(PrvtProt_task_t *task,PrvtProt_VS_t *rmtVS);

static void PP_VS_send_cb(void * para);
static uint32_t PP_VS_gpsconv(double dddmm);
/******************************************************
description�� function code
******************************************************/
/******************************************************
*��������PP_VS_init

*��  �Σ�void

*����ֵ��void

*��  ������ʼ��

*��  ע��
******************************************************/
void PP_VS_init(void)
{
	memset(&PP_rmtVS,0 , sizeof(PrvtProt_VS_t));
	memset(&PP_VS_Pack,0 , sizeof(PrvtProt_pack_t));
	memset(&PP_VS_appdata,0 , sizeof(PrvtProt_App_VS_t));
	memcpy(PP_rmtVS.pack.Header.sign,"**",2);
	PP_rmtVS.pack.Header.ver.Byte = 0x30;
	PP_rmtVS.pack.Header.commtype.Byte = 0xe1;
	PP_rmtVS.pack.Header.opera = 0x02;
	PP_rmtVS.pack.Header.tboxid = 27;
	memcpy(PP_rmtVS.pack.DisBody.aID,"130",strlen("130"));
	PP_rmtVS.pack.DisBody.eventId = PP_INIT_EVENTID;
	PP_rmtVS.pack.DisBody.appDataProVer = 256;
	PP_rmtVS.pack.DisBody.testFlag = 1;
	PP_rmtVS.state.req = PP_VS_NOREQ;
}

/******************************************************
*��������PP_VS_mainfunction

*��  �Σ�void

*����ֵ��void

*��  ������������

*��  ע��
******************************************************/
int PP_VS_mainfunction(void *task)
{
	int res;
	res = 		PP_VS_do_checksock((PrvtProt_task_t*)task) ||
				PP_VS_do_rcvMsg((PrvtProt_task_t*)task) ||
				PP_VS_do_wait((PrvtProt_task_t*)task) ||
				PP_VS_do_VehiStMainfunction((PrvtProt_task_t*)task);
	return res;
}

/******************************************************
*��������PP_VS_do_checksock

*��  �Σ�void

*����ֵ��void

*��  �������socket����

*��  ע��
******************************************************/
static int PP_VS_do_checksock(PrvtProt_task_t *task)
{
	if(1 == sockproxy_socketState())//socket open
	{
		return 0;
	}
	else
	{
		PP_rmtVS.state.req = PP_VS_NOREQ;
	}
	
	return -1;
}

/******************************************************
*��������PP_VS_do_rcvMsg

*��  �Σ�void

*����ֵ��void

*��  �����������ݺ���

*��  ע��
******************************************************/
static int PP_VS_do_rcvMsg(PrvtProt_task_t *task)
{	
	int rlen = 0;
	PrvtProt_pack_t rcv_pack;
	memset(&rcv_pack,0 , sizeof(PrvtProt_pack_t));
	if ((rlen = RdPP_queue(PP_REMOTE_VS,rcv_pack.Header.sign,sizeof(PrvtProt_pack_t))) <= 0)
    {
		return 0;
	}
	
	log_o(LOG_HOZON, "receive vehi status message");
	protocol_dump(LOG_HOZON, "PRVT_PROT", rcv_pack.Header.sign, rlen, 0);
	if((rcv_pack.Header.sign[0] != 0x2A) || (rcv_pack.Header.sign[1] != 0x2A) || \
			(rlen <= 18))
	{
		return 0;
	}
	
	if(rlen > (18 + PP_MSG_DATA_LEN))
	{
		return 0;
	}
	PP_VS_RxMsgHandle(task,&rcv_pack,rlen);

	return 0;
}

/******************************************************
*��������PP_VS_RxMsgHandle

*��  �Σ�void

*����ֵ��void

*��  �����������ݴ���

*��  ע��
******************************************************/
static void PP_VS_RxMsgHandle(PrvtProt_task_t *task,PrvtProt_pack_t* rxPack,int len)
{
	int aid;
	if(PP_OPERATETYPE_NGTP != rxPack->Header.opera)
	{
		log_e(LOG_HOZON, "unknow package");
		return;
	}

	PrvtProt_DisptrBody_t MsgDataBody;
	PrvtProt_App_VS_t Appdata;
	PrvtPro_decodeMsgData(rxPack->msgdata,(len - 18),&MsgDataBody,&Appdata);
	aid = (MsgDataBody.aID[0] - 0x30)*100 +  (MsgDataBody.aID[1] - 0x30)*10 + \
			  (MsgDataBody.aID[2] - 0x30);
	if(PP_AID_VS != aid)
	{
		log_e(LOG_HOZON, "aid unmatch");
		return;
	}

	switch(MsgDataBody.mID)
	{
		case PP_MID_VS_REQ:
		{
			PP_rmtVS.state.req = Appdata.VSReq.vehStatusReqType;
			PP_rmtVS.pack.DisBody.eventId = MsgDataBody.eventId;
			PP_rmtVS.pack.DisBody.expTime = MsgDataBody.expTime;
		}
		break;
		default:
		break;
	}
}

/******************************************************
*��������PP_xcall_do_wait

*��  �Σ�void

*����ֵ��void

*��  ��������Ƿ����¼��ȴ�Ӧ��

*��  ע��
******************************************************/
static int PP_VS_do_wait(PrvtProt_task_t *task)
{
	return 0;
}

/******************************************************
*��������PP_xcall_do_checkXcall

*��  �Σ�

*����ֵ��

*��  �������ecall������

*��  ע��
******************************************************/
static int PP_VS_do_VehiStMainfunction(PrvtProt_task_t *task)
{
	int idlenode;

	switch(PP_rmtVS.state.req)
	{
		case PP_VS_NOREQ:
		break;
		case PP_VS_BASICSTATUS:
		{
			PP_VS_appdata.VSResp.ExtSt.validFlg = 0;//
			if(0 == PP_VS_VehiStatusResp(task,&PP_rmtVS))
			{
				idlenode = PP_getIdleNode();
				PP_TxInform[idlenode].aid = PP_AID_VS;
				PP_TxInform[idlenode].mid = PP_MID_VS_RESP;
				PP_TxInform[idlenode].pakgtype = PP_TXPAKG_SIGTIME;
				PP_TxInform[idlenode].eventtime = tm_get_time();
				PP_TxInform[idlenode].idleflag = 1;
				PP_TxInform[idlenode].description = "vehi status";
				SP_data_write(PP_VS_Pack.Header.sign,PP_VS_Pack.totallen,PP_VS_send_cb,&PP_TxInform[idlenode]);
			}
			PP_rmtVS.state.req = PP_VS_NOREQ;
			log_o(LOG_HOZON, "Vehicle status response\n");
		}
		break;
		case PP_VS_EXTSTATUS:
		{
			PP_VS_appdata.VSResp.ExtSt.validFlg = 1;//report extended Vehicle Status
			if(0 == PP_VS_VehiStatusResp(task,&PP_rmtVS))
			{
				idlenode = PP_getIdleNode();
				PP_TxInform[idlenode].aid = PP_AID_VS;
				PP_TxInform[idlenode].mid = PP_MID_VS_RESP;
				PP_TxInform[idlenode].pakgtype = PP_TXPAKG_SIGTIME;
				PP_TxInform[idlenode].eventtime = tm_get_time();
				PP_TxInform[idlenode].idleflag = 1;
				PP_TxInform[idlenode].description = "vehi status etx";
				SP_data_write(PP_VS_Pack.Header.sign,PP_VS_Pack.totallen,PP_VS_send_cb,&PP_TxInform[idlenode]);
			}
			PP_rmtVS.state.req = PP_VS_NOREQ;
			log_o(LOG_HOZON, "extended Vehicle status response\n");
		}
		break;
		default:
		break;
	}

	return 0;
}

/******************************************************
*��������PP_VS_VehiStatusResp

*��  �Σ�

*����ֵ��

*��  ����remote vehi status response

*��  ע��
******************************************************/
static int PP_VS_VehiStatusResp(PrvtProt_task_t *task,PrvtProt_VS_t *rmtVS)
{
	int msgdatalen;
	int res = 0;
	/*header*/
	rmtVS->pack.Header.ver.Byte = task->version;
	rmtVS->pack.Header.nonce  = PrvtPro_BSEndianReverse((uint32_t)task->nonce);
	rmtVS->pack.Header.tboxid = PrvtPro_BSEndianReverse((uint32_t)task->tboxid);
	memcpy(&PP_VS_Pack, &rmtVS->pack.Header, sizeof(PrvtProt_pack_Header_t));
	/*body*/
	rmtVS->pack.DisBody.mID = PP_MID_VS_RESP;
	//rmtVS->pack.DisBody.eventId = PP_AID_RMTCTRL + PP_MID_RMTCTRL_RESP;
	rmtVS->pack.DisBody.eventTime = PrvtPro_getTimestamp();
	//rmtVS->pack.DisBody.expTime   = PrvtPro_getTimestamp();
	PP_UpMsgCnt++;
	rmtVS->pack.DisBody.ulMsgCnt = PP_UpMsgCnt;	/* OPTIONAL */

	/*appdata*/
	PrvtProtcfg_gpsData_t gpsDt;
	PP_VS_appdata.VSResp.statusTime = PrvtPro_getTimestamp();

	PP_VS_appdata.VSResp.gpsPos.gpsSt = PrvtProtCfg_gpsStatus();
	PP_VS_appdata.VSResp.gpsPos.gpsTimestamp = PrvtPro_getTimestamp();

	PrvtProtCfg_gpsData(&gpsDt);

	if(PP_VS_appdata.VSResp.gpsPos.gpsSt == 1)
	{
		PP_VS_appdata.VSResp.gpsPos.latitude = gpsDt.is_north?(long)(PP_VS_gpsconv(gpsDt.latitude)):(long)(PP_VS_gpsconv(gpsDt.latitude)*(-1));
		PP_VS_appdata.VSResp.gpsPos.longitude = gpsDt.is_east?(long)(PP_VS_gpsconv(gpsDt.longitude)):(long)(PP_VS_gpsconv(gpsDt.longitude)*(-1));
	}
	else
	{
		PP_VS_appdata.VSResp.gpsPos.latitude  = gb_data_gpsnorth()?(long)gb_data_gpslatitude():(long)((-1)*gb_data_gpslatitude());
		PP_VS_appdata.VSResp.gpsPos.longitude = gb_data_gpseast()?(long)gb_data_gpslongitude():(long)((-1)*gb_data_gpslongitude());
	}
	
	PP_VS_appdata.VSResp.gpsPos.altitude = (long)(gpsDt.height);
	if(PP_VS_appdata.VSResp.gpsPos.altitude > 10000)
	{
		PP_VS_appdata.VSResp.gpsPos.altitude = 10000;
	}
	PP_VS_appdata.VSResp.gpsPos.heading = (long)gpsDt.direction;
	PP_VS_appdata.VSResp.gpsPos.gpsSpeed = (long)(gpsDt.knots*10);
	PP_VS_appdata.VSResp.gpsPos.hdop = (long)(gpsDt.hdop*10);
	if(PP_VS_appdata.VSResp.gpsPos.hdop > 1000)
	{
		PP_VS_appdata.VSResp.gpsPos.hdop = 1000;
	}

	PP_VS_appdata.VSResp.basicSt.driverDoor 		= getgb_data_LFDoorOpenSt()? 1:0/* OPTIONAL */;
	PP_VS_appdata.VSResp.basicSt.driverLock 		= PrvtProtCfg_doorlockSt()?0:1;
	PP_VS_appdata.VSResp.basicSt.passengerDoor 		= getgb_data_RFDoorOpenSt()	? 1:0/* OPTIONAL */;
	PP_VS_appdata.VSResp.basicSt.passengerLock 		= PrvtProtCfg_doorlockSt()?0:1;
	PP_VS_appdata.VSResp.basicSt.rearLeftDoor 		= getgb_data_LRDoorOpenSt()? 1:0	/* OPTIONAL */;
	PP_VS_appdata.VSResp.basicSt.rearLeftLock 		= PrvtProtCfg_doorlockSt()?0:1;
	PP_VS_appdata.VSResp.basicSt.rearRightDoor 		= getgb_data_RRDoorOpenSt()? 1:0	/* OPTIONAL */;
	PP_VS_appdata.VSResp.basicSt.rearRightLock 		= PrvtProtCfg_doorlockSt()?0:1;
	PP_VS_appdata.VSResp.basicSt.bootStatus 		= PrvtProtCfg_reardoorSt()	/* OPTIONAL */;
	PP_VS_appdata.VSResp.basicSt.bootStatusLock 	= gb_data_reardoorlockSt();
	PP_VS_appdata.VSResp.basicSt.driverWindow 		= 0	/* OPTIONAL */;
	PP_VS_appdata.VSResp.basicSt.passengerWindow 	= 0	/* OPTIONAL */;
	PP_VS_appdata.VSResp.basicSt.rearLeftWindow 	= 0	/* OPTIONAL */;
	PP_VS_appdata.VSResp.basicSt.rearRightWinow 	= 0	/* OPTIONAL */;
	PP_VS_appdata.VSResp.basicSt.sunroofStatus 		= PrvtProtCfg_sunroofSt()	/* OPTIONAL */;
	if(PP_rmtCtrl_cfg_RmtStartSt() == 0)
	{
		PP_VS_appdata.VSResp.basicSt.engineStatus = 0;
	}
	else
	{
		PP_VS_appdata.VSResp.basicSt.engineStatus = 1;
	}
	PP_VS_appdata.VSResp.basicSt.accStatus 			= PP_rmtCtrl_cfg_ACOnOffSt();
	PP_VS_appdata.VSResp.basicSt.accTemp 			= PrvtProtCfg_LHTemp()	/* OPTIONAL */;//16-32
	PP_VS_appdata.VSResp.basicSt.accMode 			= gb_data_ACMode()	/* OPTIONAL */;
	if(PP_VS_appdata.VSResp.basicSt.accMode > 3)
	{
		PP_VS_appdata.VSResp.basicSt.accMode = 3;
	}
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.accMode  = %ld",PP_VS_appdata.VSResp.basicSt.accMode);
	PP_VS_appdata.VSResp.basicSt.accBlowVolume		= gb_data_BlowerGears()/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.accBlowVolume  = %ld",PP_VS_appdata.VSResp.basicSt.accBlowVolume);
	PP_VS_appdata.VSResp.basicSt.innerTemp 			= gb_data_InnerTemp();
	if(PP_VS_appdata.VSResp.basicSt.innerTemp > 125)
	{
		PP_VS_appdata.VSResp.basicSt.innerTemp = 125;
	}
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.innerTemp  = %ld",PP_VS_appdata.VSResp.basicSt.innerTemp);
	
	PP_VS_appdata.VSResp.basicSt.outTemp 			= gb_data_outTemp();
	if(PP_VS_appdata.VSResp.basicSt.outTemp  > 125)
	{
		PP_VS_appdata.VSResp.basicSt.outTemp  = 125;
	}
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.outTemp  = %ld",PP_VS_appdata.VSResp.basicSt.outTemp);
	
			
	PP_VS_appdata.VSResp.basicSt.sideLightStatus	= gb_data_PostionLampSt();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.sideLightStatus  = %ld",PP_VS_appdata.VSResp.basicSt.sideLightStatus);
	PP_VS_appdata.VSResp.basicSt.dippedBeamStatus	= gb_data_NearLampSt();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.dippedBeamStatus  = %ld",PP_VS_appdata.VSResp.basicSt.dippedBeamStatus);
	PP_VS_appdata.VSResp.basicSt.mainBeamStatus		= gb_data_HighbeamLampSt();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.mainBeamStatus  = %ld",PP_VS_appdata.VSResp.basicSt.mainBeamStatus);
	PP_VS_appdata.VSResp.basicSt.hazardLightStus	= gb_data_TwinFlashLampSt();//双闪灯
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.hazardLightStus  = %ld",PP_VS_appdata.VSResp.basicSt.hazardLightStus);
	PP_VS_appdata.VSResp.basicSt.frtRightTyrePre	= PrvtProtCfg_TyrePre(1)/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.frtRightTyrePre  = %ld",PP_VS_appdata.VSResp.basicSt.frtRightTyrePre);
	PP_VS_appdata.VSResp.basicSt.frtRightTyreTemp	= PrvtProtCfg_TyreTemp(1)/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.frtRightTyreTemp  = %ld",PP_VS_appdata.VSResp.basicSt.frtRightTyreTemp);
	PP_VS_appdata.VSResp.basicSt.frontLeftTyrePre	= PrvtProtCfg_TyrePre(2)/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.frontLeftTyrePre  = %ld",PP_VS_appdata.VSResp.basicSt.frontLeftTyrePre);
	PP_VS_appdata.VSResp.basicSt.frontLeftTyreTemp	= PrvtProtCfg_TyreTemp(2)	/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.frontLeftTyreTemp  = %ld",PP_VS_appdata.VSResp.basicSt.frontLeftTyreTemp);
	PP_VS_appdata.VSResp.basicSt.rearRightTyrePre	= PrvtProtCfg_TyrePre(3)/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.rearRightTyrePre  = %ld",PP_VS_appdata.VSResp.basicSt.rearRightTyrePre);
	PP_VS_appdata.VSResp.basicSt.rearRightTyreTemp	= PrvtProtCfg_TyreTemp(3)	/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.rearRightTyreTemp  = %ld",PP_VS_appdata.VSResp.basicSt.rearRightTyreTemp);
	PP_VS_appdata.VSResp.basicSt.rearLeftTyrePre	= PrvtProtCfg_TyrePre(4)/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.rearLeftTyrePre  = %ld",PP_VS_appdata.VSResp.basicSt.rearLeftTyrePre);
	PP_VS_appdata.VSResp.basicSt.rearLeftTyreTemp	= PrvtProtCfg_TyreTemp(4)/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.rearLeftTyreTemp  = %ld",PP_VS_appdata.VSResp.basicSt.rearLeftTyreTemp);
	
	long VehicleSOC;
	VehicleSOC = PrvtProtCfg_vehicleSOC();
	PP_VS_appdata.VSResp.basicSt.batterySOCExact	= VehicleSOC * 100;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.batterySOCExact  = %ld",PP_VS_appdata.VSResp.basicSt.batterySOCExact);
	PP_VS_appdata.VSResp.basicSt.chargeRemainTim	= PrvtProtCfg_ACChargeRemainTime()/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.chargeRemainTim  = %ld",PP_VS_appdata.VSResp.basicSt.chargeRemainTim);
	PP_VS_appdata.VSResp.basicSt.availableOdomtr	= PrvtProtCfg_ResidualOdometer();//续航里程
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.availableOdomtr  = %ld",PP_VS_appdata.VSResp.basicSt.availableOdomtr);
	PP_VS_appdata.VSResp.basicSt.engineRunningTime	= 1/* OPTIONAL */;
	PP_VS_appdata.VSResp.basicSt.bookingChargeSt	= GetPP_ChargeCtrl_appointSt();
	PP_VS_appdata.VSResp.basicSt.bookingChargeHour	= GetPP_ChargeCtrl_appointHour()	/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.bookingChargeHour  = %ld",PP_VS_appdata.VSResp.basicSt.bookingChargeHour);
	PP_VS_appdata.VSResp.basicSt.bookingChargeMin	= GetPP_ChargeCtrl_appointMin()/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.bookingChargeMin  = %ld",PP_VS_appdata.VSResp.basicSt.bookingChargeMin);
	PP_VS_appdata.VSResp.basicSt.chargeMode			= PrvtProtCfg_chargeSt()/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.chargeMode  = %ld",PP_VS_appdata.VSResp.basicSt.chargeMode);
	PP_VS_appdata.VSResp.basicSt.chargeStatus 		= gb_data_chargestatus();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.chargeStatus  = %ld",PP_VS_appdata.VSResp.basicSt.chargeStatus);
	PP_VS_appdata.VSResp.basicSt.powerMode			= gb_data_powermode();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.powerMode  = %ld",PP_VS_appdata.VSResp.basicSt.powerMode);
	PP_VS_appdata.VSResp.basicSt.speed				= PrvtProtCfg_vehicleSpeed();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.speed  = %ld",PP_VS_appdata.VSResp.basicSt.speed);
	PP_VS_appdata.VSResp.basicSt.totalOdometer		= PrvtProtCfg_TotalOdometer();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.totalOdometer  = %ld",PP_VS_appdata.VSResp.basicSt.totalOdometer);
	PP_VS_appdata.VSResp.basicSt.batteryVoltage		= PrvtProtCfg_TotalVoltage();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.batteryVoltage  = %ld",PP_VS_appdata.VSResp.basicSt.batteryVoltage);
	PP_VS_appdata.VSResp.basicSt.batteryCurrent		= PrvtProtCfg_TotalCurrent();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.batteryCurrent  = %ld",PP_VS_appdata.VSResp.basicSt.batteryCurrent);
	PP_VS_appdata.VSResp.basicSt.batterySOCPrc		= VehicleSOC;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.batterySOCPrc  = %ld",PP_VS_appdata.VSResp.basicSt.batterySOCPrc);
	PP_VS_appdata.VSResp.basicSt.dcStatus			= (gb_data_dcdcstatus() == 0x1)?1:0;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.dcStatus  = %ld",PP_VS_appdata.VSResp.basicSt.dcStatus);
	PP_VS_appdata.VSResp.basicSt.gearPosition		= gb_data_gearPosition();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.gearPosition  = %ld",PP_VS_appdata.VSResp.basicSt.gearPosition);

	PP_VS_appdata.VSResp.basicSt.insulationRstance	= gb_data_insulationResistance();
	if(PP_VS_appdata.VSResp.basicSt.insulationRstance > 60000)
	{
		PP_VS_appdata.VSResp.basicSt.insulationRstance = 60000;
	}
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.insulationRstance  = %ld",PP_VS_appdata.VSResp.basicSt.insulationRstance);

	PP_VS_appdata.VSResp.basicSt.acceleratePedalprc	= gb_data_acceleratePedalPrc();
	if(PP_VS_appdata.VSResp.basicSt.acceleratePedalprc > 100)
	{
		PP_VS_appdata.VSResp.basicSt.acceleratePedalprc = 100;
	}
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.acceleratePedalprc  = %ld",PP_VS_appdata.VSResp.basicSt.acceleratePedalprc);

	PP_VS_appdata.VSResp.basicSt.deceleratePedalprc	= gb_data_deceleratePedalPrc();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.deceleratePedalprc  = %ld",PP_VS_appdata.VSResp.basicSt.deceleratePedalprc);
	PP_VS_appdata.VSResp.basicSt.canBusActive		= gb_data_CanbusActiveSt();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.canBusActive  = %ld",PP_VS_appdata.VSResp.basicSt.canBusActive);
	PP_VS_appdata.VSResp.basicSt.bonnetStatus		= 0;//引擎盖，默认关
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.bonnetStatus  = %ld",PP_VS_appdata.VSResp.basicSt.bonnetStatus);
	PP_VS_appdata.VSResp.basicSt.lockStatus			= PrvtProtCfg_doorlockSt();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.lockStatus  = %ld",PP_VS_appdata.VSResp.basicSt.lockStatus);
	PP_VS_appdata.VSResp.basicSt.gsmStatus			= gb32960_networkSt();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.gsmStatus  = %ld",PP_VS_appdata.VSResp.basicSt.gsmStatus);
	PP_VS_appdata.VSResp.basicSt.wheelTyreMotrSt	= getgb_data_bdmsystemfailure();	/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.wheelTyreMotrSt  = %ld",PP_VS_appdata.VSResp.basicSt.wheelTyreMotrSt);
	PP_VS_appdata.VSResp.basicSt.vehicleAlarmSt		= getgb_data_warnSt();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.vehicleAlarmSt  = %ld",PP_VS_appdata.VSResp.basicSt.vehicleAlarmSt);
	PP_VS_appdata.VSResp.basicSt.currentJourneyID	= gb_data_trip();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.currentJourneyID  = %ld",PP_VS_appdata.VSResp.basicSt.currentJourneyID);
	PP_VS_appdata.VSResp.basicSt.journeyOdom		= PrvtProtCfg_trip(); /*里程*/
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.journeyOdom  = %ld",PP_VS_appdata.VSResp.basicSt.journeyOdom);
	PP_VS_appdata.VSResp.basicSt.frtLeftSeatHeatLel	= PP_rmtCtrl_cfg_DrivHeatingSt()	/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.frtLeftSeatHeatLel  = %ld",PP_VS_appdata.VSResp.basicSt.frtLeftSeatHeatLel);
	PP_VS_appdata.VSResp.basicSt.frtRightSeatHeatLel = PP_rmtCtrl_cfg_PassHeatingSt()/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.frtRightSeatHeatLel  = %ld",PP_VS_appdata.VSResp.basicSt.frtRightSeatHeatLel);
	PP_VS_appdata.VSResp.basicSt.airCleanerSt		= PrvtProt_SignParse_pm25valid()/* OPTIONAL */;
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.airCleanerSt  = %ld",PP_VS_appdata.VSResp.basicSt.airCleanerSt);
	PP_VS_appdata.VSResp.basicSt.srsStatus 			= PrvtProtCfg_CrashOutputSt();
	log_i(LOG_HOZON, "PP_VS_appdata.VSResp.basicSt.srsStatus  = %ld",PP_VS_appdata.VSResp.basicSt.srsStatus);

	uint8_t i;
	if(PP_VS_appdata.VSResp.ExtSt.validFlg == 1)//上报故障报警
	{
		log_i(LOG_HOZON, "read warn status\n");
		PP_VS_appdata.VSResp.ExtSt.alertSize = 0;//
		for(i = 0;i < GB32960_VS_WARN_NUM;i++)
		{
			if(gb_fault.warn[i])
			{
				PP_VS_appdata.VSResp.ExtSt.alertIds[PP_VS_appdata.VSResp.ExtSt.alertSize] = i;
				log_i(LOG_HOZON, "PP_VS_appdata.VSResp.ExtSt.alertIds[%d] = %d\n",PP_VS_appdata.VSResp.ExtSt.alertSize,i);
				PP_VS_appdata.VSResp.ExtSt.alertSize++;
			}
		}
	}

	if(0 != PrvtPro_msgPackageEncoding(ECDC_RMTVS_RESP,PP_VS_Pack.msgdata,&msgdatalen,\
									   &rmtVS->pack.DisBody,&PP_VS_appdata))//���ݱ������Ƿ����
	{
		log_e(LOG_HOZON, "uper error");
		return -1;
	}

	PP_VS_Pack.totallen = 18 + msgdatalen;
	PP_VS_Pack.Header.msglen = PrvtPro_BSEndianReverse((long)(18 + msgdatalen));

	return res;
}

/******************************************************
*��������PP_VS_SetVSReq

*��  �Σ�

*����ֵ��

*��  ��������ecall ����

*��  ע��
******************************************************/
void PP_VS_SetVSReq(unsigned char req)
{
	PP_rmtVS.state.req = req;
	PP_rmtVS.pack.DisBody.eventId = 130+2;
	log_o(LOG_HOZON, "receive vehicle status query request\n");
}

/******************************************************
*��������PP_VS_send_cb

*��  �Σ�

*����ֵ��

*��  ����

*��  ע��
******************************************************/
static void PP_VS_send_cb(void * para)
{
	PrvtProt_TxInform_t *TxInform_ptr = (PrvtProt_TxInform_t*)para;
	log_i(LOG_HOZON, "aid = %d",TxInform_ptr->aid);
	log_i(LOG_HOZON, "mid = %d",TxInform_ptr->mid);
	log_i(LOG_HOZON, "pakgtype = %d",TxInform_ptr->pakgtype);
	log_i(LOG_HOZON, "eventtime = %d",TxInform_ptr->eventtime);
	log_i(LOG_HOZON, "successflg = %d",TxInform_ptr->successflg);
	log_i(LOG_HOZON, "failresion = %d",TxInform_ptr->failresion);
	log_i(LOG_HOZON, "txfailtime = %d",TxInform_ptr->txfailtime);

	TxInform_ptr->idleflag = 0;
}


/* Convert dddmm.mmmm(double) To ddd.dd+(double)*/
static uint32_t PP_VS_gpsconv(double dddmm)
{
    int deg;
    double min;

    deg = dddmm / 100.0;
    min = dddmm - deg * 100;

    return (uint32_t)((deg + min / 60 + 0.5E-6) * 1000000);
}