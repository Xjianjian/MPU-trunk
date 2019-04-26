/******************************************************
文件名：	PrvtProt.c

描述：	企业私有协议（浙江合众）	
Data			Vasion			author
2018/1/10		V1.0			liujian
*******************************************************/

/*******************************************************
description： include the header file
*******************************************************/
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include  <errno.h>
#include <sys/times.h>
#include "timer.h"
#include <sys/prctl.h>

#include <sys/types.h>
#include <sysexits.h>	/* for EX_* exit codes */
#include <assert.h>	/* for assert(3) */
#include "constr_TYPE.h"
#include "asn_codecs.h"
#include "asn_application.h"
#include "asn_internal.h"	/* for _ASN_DEFAULT_STACK_MAX */
//#include "MessageData.h"
#include "Appdatainfo.h"
#include "Bodyinfo.h"
#include "per_encoder.h"
#include "per_decoder.h"

#include "init.h"
#include "log.h"
#include "list.h"
//#include "gb32960.h"
#include "PrvtProt.h"
#include "../../support/protocol.h"
#include "hozon_SP_api.h"
#include "shell_api.h"
/*******************************************************
description： global variable definitions
*******************************************************/

/*******************************************************
description： static variable definitions
*******************************************************/
//static PrvtProt_heartbeat_t heartbeat;
static PrvtProt_task_t pp_task;
static PrvtProt_pack_t PP_RxPack;

//static uint8_t tboxEncodeData[PP_MSG_DATA_LEN];
//static int tboxEncodeLen;
static uint8_t tboxAppdata[PP_MSG_DATA_LEN];
static int tboxAppdataLen;
static uint8_t tboxDisBodydata[PP_MSG_DATA_LEN];
static int tboxDisBodydataLen;
/*******************************************************
description： function declaration
*******************************************************/
/*Global function declaration*/

/*Static function declaration*/
#if PP_THREAD
static void *PrvtProt_main(void);
#endif
static int PrvtPro_do_checksock(PrvtProt_task_t *task);
static int PrvtPro_do_rcvMsg(PrvtProt_task_t *task);
static int PrvtProt_do_heartbeat(PrvtProt_task_t *task);
static void PrvtPro_RxMsgHandle(PrvtProt_task_t *task,PrvtProt_pack_t* rxPack,int len);
static int PrvtPro_do_wait(PrvtProt_task_t *task);
static uint32_t PrvtPro_BSEndianReverse(uint32_t value);
static int PP_shell_setCtrlParameter(int argc, const char **argv);
static void PrvtPro_makeUpPack(PrvtProt_pack_t *RxPack,uint8_t* input,int len);

static int PrvtPro_XcallReq(PrvtProt_task_t *task);
static int PrvtPro_writeout(const void *buffer,size_t size,void *key);
/******************************************************
description： function code
******************************************************/
/******************************************************
*函数名：PrvtProt_init

*形  参：void

*返回值：void

*描  述：初始化

*备  注：
******************************************************/
int PrvtProt_init(INIT_PHASE phase)
{
    int ret = 0;

    switch (phase)
    {
        case INIT_PHASE_INSIDE:
		{
			pp_task.heartbeat.ackFlag = 0;
			pp_task.heartbeat.state = 0;//
			pp_task.heartbeat.period = PP_HEART_BEAT_TIME;//
			pp_task.heartbeat.timer = tm_get_time();
			pp_task.waitSt = PP_IDLE;
			pp_task.waittime = 0;
			pp_task.suspend = 0;
			pp_task.nonce = 0;/* TCP会话ID 由TSP平台产生 */
			pp_task.version = 0x30;/* 大/小版本(由TSP平台定义)*/
			pp_task.tboxid = 204;/* 平台通过tboxID与tboxSN映射 */
		}
        break;
        case INIT_PHASE_RESTORE:
        break;
        case INIT_PHASE_OUTSIDE:
		{
			 ret |= shell_cmd_register("HOZON_PP_SET", PP_shell_setCtrlParameter, "set HOZON PrvtProt control parameter");
		}
        break;
    }

    return ret;
}


/******************************************************
*函数名：PrvtProt_run

*形  参：void

*返回值：void

*描  述：创建任务线程

*备  注：
******************************************************/
int PrvtProt_run(void)
{ 	
	int res = 0;
#if PP_THREAD
	int ret = 0;
    pthread_t tid;
    pthread_attr_t ta;

    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);

    ret = pthread_create(&tid, &ta, (void *)PrvtProt_main, NULL);

    if (ret != 0)
    {
        log_e(LOG_HOZON, "pthread_create failed, error: %s", strerror(errno));
        return ret;
    }
#else
	res = 	PrvtPro_do_rcvMsg(&pp_task) ||
			PrvtPro_do_wait(&pp_task) || 
			PrvtProt_do_heartbeat(&pp_task);
#endif
    return res;
}

#if PP_THREAD
/******************************************************
*函数名：PrvtProt_main

*形  参：void

*返回值：void

*描  述：主任务函数

*备  注：
******************************************************/
static void *PrvtProt_main(void)
{
	log_o(LOG_HOZON, "proprietary protocol  of hozon thread running");
	int res;
    prctl(PR_SET_NAME, "HZ_PRVT_PROT");
    while (1)
    {
		if(pp_task.suspend != 0)
		{
			continue;
		}
		log_set_level(LOG_HOZON, LOG_DEBUG);
		res = 	PrvtPro_do_checksock(&pp_task) ||
				PrvtPro_do_rcvMsg(&pp_task) ||
				PrvtPro_do_wait(&pp_task) || 
				PrvtProt_do_heartbeat(&pp_task) ||
				PrvtPro_XcallReq(&pp_task);
		
    }
	(void)res;
    return NULL;
}
#endif

/******************************************************
*函数名：PrvtProt_do_heartbeat

*形  参：void

*返回值：void

*描  述：心跳任务

*备  注：
******************************************************/
static int PrvtProt_do_heartbeat(PrvtProt_task_t *task)
{
	PrvtProt_pack_Header_t pack_Header;

	if((tm_get_time() - task->heartbeat.timer) > (task->heartbeat.period*1000))
	{
		pack_Header.sign[0] = 0x2A;
		pack_Header.sign[1] = 0x2A;
		pack_Header.ver.Byte = task->version;
		pack_Header.nonce  = PrvtPro_BSEndianReverse(task->nonce);
		pack_Header.commtype.Byte = 0x70;
		pack_Header.safetype.Byte = 0x00;
		pack_Header.opera = 0x01;
		pack_Header.msglen = PrvtPro_BSEndianReverse((uint32_t)18);
		pack_Header.tboxid = PrvtPro_BSEndianReverse(task->tboxid);
		
		if(sockproxy_MsgSend(pack_Header.sign, 18,NULL) > 0)//发送成功
		{
			protocol_dump(LOG_HOZON, "PRVT_PROT", pack_Header.sign, 18, 1);
			task->waitSt = PP_HEARTBEAT;
			task->heartbeat.ackFlag = PP_ACK_WAIT;
			task->waittime = tm_get_time();		
		}	
		task->heartbeat.timer = tm_get_time();
		return -1;
	}
	return 0;
}

/******************************************************
*函数名：PrvtPro_do_checksock

*形  参：void

*返回值：void

*描  述：检查socket连接

*备  注：
******************************************************/
static int PrvtPro_do_checksock(PrvtProt_task_t *task)
{
	if(1 == sockproxy_socketState())//socket open
	{
		
		return 0;
	}

	return -1;
}

/******************************************************
*函数名：PrvtPro_do_rcvMsg

*形  参：void

*返回值：void

*描  述：接收数据函数

*备  注：
******************************************************/
static int PrvtPro_do_rcvMsg(PrvtProt_task_t *task)
{	
	int rlen = 0;
	uint8_t rcvbuf[1456U] = {0};
	
	if ((rlen = PrvtProt_rcvMsg(rcvbuf,1456)) <= 0)
    {
		return 0;
	}
	
	log_i(LOG_HOZON, "HOZON private protocol receive message");
	protocol_dump(LOG_HOZON, "PRVT_PROT", rcvbuf, rlen, 0);
	if((rcvbuf[0] != 0x2A) || (rcvbuf[1] != 0x2A) || \
			(rlen < 18))//判断数据帧头有误或者数据长度不对
	{
		return 0;
	}
	
	if(rlen > (18 + PP_MSG_DATA_LEN))//接收数据长度超出缓存buffer长度
	{
		return 0;
	}
	PrvtPro_makeUpPack(&PP_RxPack,rcvbuf,rlen);
	protocol_dump(LOG_HOZON, "PRVT_PROT", PP_RxPack.packHeader.sign, rlen, 0);
	PrvtPro_RxMsgHandle(task,&PP_RxPack,rlen);

	return 0;
}

/******************************************************
*函数名：PrvtPro_makeUpPack

*形  参：void

*返回值：void

*描  述：接收数据解包

*备  注：
******************************************************/
static void PrvtPro_makeUpPack(PrvtProt_pack_t *RxPack,uint8_t* input,int len)
{
	static int rlen = 0;
	uint8_t rcvstep = 0;

	rlen = 0;
	RxPack->packHeader.sign[0] = input[rlen++];
	RxPack->packHeader.sign[1] = input[rlen++];
	len = len-2;
	while(len--)
	{
		switch(rcvstep)
		{
			case 0://接收版本号
			{
				RxPack->packHeader.ver.Byte = input[rlen++];
				rcvstep = 1;
			}
			break;
			case 1://接收tcp会话id
			{
				RxPack->packHeader.nonce = PrvtPro_BSEndianReverse(*((uint32_t*)(&input[rlen])));
				rlen += 4;
				rcvstep = 2;
			}
			break;	
			case 2://编码、连接等方式
			{
				RxPack->packHeader.commtype.Byte = input[rlen++];
				rcvstep = 3;
			}
			break;	
			case 3://加密、签名方式
			{
				RxPack->packHeader.safetype.Byte = input[rlen++];
				rcvstep = 4;
			}
			break;
			case 4://操作类型
			{
				RxPack->packHeader.opera = input[rlen++];
				rcvstep = 5;
			}
			break;
			case 5://报文长度
			{
				RxPack->packHeader.msglen = PrvtPro_BSEndianReverse(*((uint32_t*)(&input[rlen])));
				rlen += 4;
				rcvstep = 6;
			}
			break;
			case 6://tboxid
			{
				RxPack->packHeader.tboxid = PrvtPro_BSEndianReverse(*((uint32_t*)(&input[rlen])));
				rlen += 4;
				rcvstep = 7;
			}
			break;
			case 7://message data
			{
				RxPack->msgdata[rlen-18] = input[rlen];
				rlen += 1;
			}
			break;
			default:
			break;
		}
	}
}

/******************************************************
*函数名：PrvtPro_rcvMsgCallback

*形  参：void

*返回值：void

*描  述：接收数据处理

*备  注：
******************************************************/
static void PrvtPro_RxMsgHandle(PrvtProt_task_t *task,PrvtProt_pack_t* rxPack,int len)
{
	switch(rxPack->packHeader.opera)
	{
		case PP_NATIONALSTANDARD_TYPE:
		{
			
		}
		break;
		case PP_HEARTBEAT_TYPE://接收到心跳包
		{
			log_i(LOG_HOZON, "heart beat is ok");
			task->heartbeat.state = 1;//正常心跳
			task->waitSt = 0;
		}
		break;
		case PP_NGTP_TYPE://ngtp
		{
			
		}
		break;
		default:
		{
			log_e(LOG_HOZON, "unknow package");
		}
		break;
	}
}

/******************************************************
*函数名：PrvtPro_do_wait

*形  参：void

*返回值：void

*描  述：检查是否有事件等待应答

*备  注：
******************************************************/
static int PrvtPro_do_wait(PrvtProt_task_t *task)
{
    if (!task->waitSt)//没有事件等待应答
    {
        return 0;
    }

    if((tm_get_time() - task->waittime) > PP_WAIT_TIMEOUT)
    {
        if (task->waitSt == PP_HEARTBEAT)
        {
            task->waitSt = PP_IDLE;
			task->heartbeat.state = 0;//心跳不正常
            log_e(LOG_HOZON, "heartbeat time out");
        }
        else
        {
			
		}
    }
	 return -1;
}

/******************************************************
*函数名：PrvtPro_BSEndianReverse

*形  参：void

*返回值：void

*描  述：大端模式和小端模式互相转换

*备  注：
******************************************************/
static uint32_t PrvtPro_BSEndianReverse(uint32_t value)
{
	return (value & 0x000000FFU) << 24 | (value & 0x0000FF00U) << 8 | \
			(value & 0x00FF0000U) >> 8 | (value & 0xFF000000U) >> 24;
}

/******************************************************
*函数名：PP_shell_setCtrlParamter

*形  参：
argv[0] - 心跳周期 ，单位:秒
argv[1] - ：是否暂停


*返回值：void

*描  述：

*备  注：
******************************************************/
static int PP_shell_setCtrlParameter(int argc, const char **argv)
{
	char period;
    if (argc != 2)
    {
        shellprintf(" usage: HOZON_PP_SET <heartbeat period> <suspend>\r\n");
        return -1;
    }
	
	sscanf(argv[0], "%c", &period);
	if(period == 0)
	{
		 shellprintf(" usage: heartbeat period invalid\r\n");
		 return -1;
	}	
	pp_task.heartbeat.period = period;
	
	sscanf(argv[1], "%c", &pp_task.suspend);
    sleep(1);

    return 0;
}

/******************************************************
*函数名：static void PrvtPro_XcallReq

*形  参：

*返回值：

*描  述：

*备  注：
******************************************************/
static int PrvtPro_XcallReq(PrvtProt_task_t *task)
{
	static asn_TYPE_descriptor_t *pduType_Body = &asn_DEF_Bodyinfo;
	static asn_TYPE_descriptor_t *pduType_appData = &asn_DEF_Appdatainfo;
	PrvtProt_pack_t pack;
	PrvtProt_DisptrBody_t	PP_DisptrBody;
	Bodyinfo_t Bodydata;
	Appdatainfo_t Appdata;

	static uint64_t timeout = 0;
	int i;
	if((tm_get_time() - timeout) > (5*1000))
	{
		memset(&Bodydata,0 , sizeof(Bodyinfo_t));
		memset(&Appdata,0 , sizeof(Appdatainfo_t));

		pack.packHeader.sign[0] = 0x2A;
		pack.packHeader.sign[1] = 0x2A;
		pack.packHeader.ver.Byte = task->version;
		pack.packHeader.nonce  = PrvtPro_BSEndianReverse(task->nonce);
		pack.packHeader.commtype.Byte = 0xe1;
		pack.packHeader.safetype.Byte = 0x00;
		pack.packHeader.opera = 0x02;
		pack.packHeader.msglen = PrvtPro_BSEndianReverse((uint32_t)18);
		pack.packHeader.tboxid = PrvtPro_BSEndianReverse(task->tboxid);
		
		PP_DisptrBody.aID[0] = '1';
		PP_DisptrBody.aID[1] = '7';
		PP_DisptrBody.aID[2] = '0';
		PP_DisptrBody.eventId = 100;
		PP_DisptrBody.ulMsgCnt = 1;	/* OPTIONAL */
		PP_DisptrBody.msgCntAcked	= 0;/* OPTIONAL */
		PP_DisptrBody.ackReq = 1;	/* OPTIONAL */
		PP_DisptrBody.appDataLen = 0;	/* OPTIONAL */
		PP_DisptrBody.appDataEncode = 0;	/* OPTIONAL */
		PP_DisptrBody.appDataProVer = 1;	/* OPTIONAL */
		PP_DisptrBody.testFlag	= 0;/* OPTIONAL */
		PP_DisptrBody.result = 0;	/* OPTIONAL */
		
		Bodydata.aID.buf = PP_DisptrBody.aID;
		Bodydata.aID.size = 3;
		Bodydata.mID = 1;
		Bodydata.eventTime = 1556171208;
		Bodydata.eventId = &(PP_DisptrBody.eventId);
		Bodydata.ulMsgCnt = &(PP_DisptrBody.ulMsgCnt);	/* OPTIONAL */
		//Bodydata.dlMsgCnt = NULL;
		Bodydata.msgCntAcked	= &(PP_DisptrBody.msgCntAcked);/* OPTIONAL */
		Bodydata.ackReq	= &(PP_DisptrBody.ackReq);/* OPTIONAL */
		Bodydata.appDataLen = &(PP_DisptrBody.appDataLen);
		Bodydata.appDataEncode	= &(PP_DisptrBody.appDataEncode);/* OPTIONAL */
		Bodydata.appDataProVer	= &(PP_DisptrBody.appDataProVer);/* OPTIONAL */
		Bodydata.testFlag	= &(PP_DisptrBody.testFlag);/* OPTIONAL */
		Bodydata.result	= &(PP_DisptrBody.result);/* OPTIONAL */
		
		static uint8_t key;
		asn_enc_rval_t ec;
		Appdata.present = Appdatainfo_PR_xcallReq;
		Appdata.choice.xcallReq.xcallType = 1;
		log_i(LOG_HOZON, "uper encode:appdata");
		key = PP_ENCODE_APPDATA;
		ec = uper_encode(pduType_appData,(void *) &Appdata,PrvtPro_writeout,&key);
		if(ec.encoded  == -1) 
		{
			log_e(LOG_HOZON, "Could not encode MessageFrame");
		}
		PP_DisptrBody.appDataLen = tboxAppdataLen;
		protocol_dump(LOG_HOZON, "uper encode:appdata", tboxAppdata,tboxAppdataLen, 0);
		log_i(LOG_HOZON, "uper encode appdata end");
		
		log_i(LOG_HOZON, "uper encode:dis body");
		Bodydata.appDataLen = &(PP_DisptrBody.appDataLen);
		key = PP_ENCODE_DISBODY;
		ec = uper_encode(pduType_Body,(void *) &Bodydata,PrvtPro_writeout,&key);
		if(ec.encoded  == -1) 
		{
			log_e(LOG_HOZON, "Could not encode MessageFrame");
		}
		protocol_dump(LOG_HOZON, "uper encode:dis body", tboxDisBodydata, tboxDisBodydataLen, 0);
		log_i(LOG_HOZON, "uper encode dis body end");
		
		log_i(LOG_HOZON, "uper decode");
		asn_codec_ctx_t *asn_codec_ctx = 0 ;
		Bodyinfo_t RxBodydata;
		Bodyinfo_t *RxBodydata_ptr = &RxBodydata;
		Appdatainfo_t RxAppdata;
		Appdatainfo_t *RxAppdata_ptr = &RxAppdata;
		uint8_t decodeBuff[50U];
		memset(&RxBodydata,0 , sizeof(Bodyinfo_t));
		memset(&RxAppdata,0 , sizeof(Appdatainfo_t));
		log_i(LOG_HOZON, "uper decode:appdata");
		uper_decode(asn_codec_ctx,pduType_appData,(void *) &RxAppdata_ptr,tboxAppdata,tboxAppdataLen,0,0);
		log_i(LOG_HOZON, "uper decode:bodydata");
		uper_decode(asn_codec_ctx,pduType_Body,(void *) &RxBodydata_ptr,tboxDisBodydata,tboxDisBodydataLen,0,0);
		decodeBuff[0] = RxBodydata.aID.buf[0];
		decodeBuff[1] = RxBodydata.aID.buf[1];
		decodeBuff[2] = RxBodydata.aID.buf[2];
		decodeBuff[3] = RxBodydata.mID;

		protocol_dump(LOG_HOZON, "uper decode", decodeBuff, 4, 0);
		log_i(LOG_HOZON, "RxBodydata.eventTime = %d",RxBodydata.eventTime);
		log_i(LOG_HOZON, "RxBodydata.eventId = %d",*RxBodydata.eventId);
		log_i(LOG_HOZON, "RxBodydata.ulMsgCnt = %d",(uint16_t)(*(RxBodydata.ulMsgCnt)));
		log_i(LOG_HOZON, "RxBodydata.msgCntAcked = %d",(uint16_t)(*(RxBodydata.msgCntAcked)));
		log_i(LOG_HOZON, "RxBodydata.ackReq = %d",(uint8_t)(*(RxBodydata.ackReq)));
		log_i(LOG_HOZON, "RxBodydata.appDataLen = %d",(uint16_t)(*(RxBodydata.appDataLen)));
		log_i(LOG_HOZON, "RxBodydata.appDataEncode = %d",(uint8_t)(*(RxBodydata.appDataEncode)));
		log_i(LOG_HOZON, "RxBodydata.appDataProVer = %d",(uint16_t)(*(RxBodydata.appDataProVer)));
		log_i(LOG_HOZON, "RxBodydata.testFlag = %d",(uint8_t)(*(RxBodydata.testFlag)));
		log_i(LOG_HOZON, "RxBodydata.result = %d",(uint16_t)(*(RxBodydata.result)));
		
		log_i(LOG_HOZON, "RxAppdata.present = %d",(uint8_t)(RxAppdata.present));
		log_i(LOG_HOZON, "xcallReq.xcallType = %d",(uint8_t)(RxAppdata.choice.xcallReq.xcallType));
		log_i(LOG_HOZON, "uper decode end");
		
		int tboxmsglen = 0;
		pack.msgdata[tboxmsglen++] = tboxDisBodydataLen +1;
		for(i = 0;i < tboxDisBodydataLen;i++)
		{
			pack.msgdata[tboxmsglen++]= tboxDisBodydata[i];
		}
		for(i = 0;i < tboxAppdataLen;i++)
		{
			pack.msgdata[tboxmsglen++]= tboxAppdata[i];
		}
		pack.packHeader.msglen = PrvtPro_BSEndianReverse((uint32_t)(18 + 1 + tboxDisBodydataLen + tboxAppdataLen));
		if(sockproxy_MsgSend(pack.packHeader.sign, 18 + 1 + tboxDisBodydataLen + tboxAppdataLen ,NULL) > 0)//发送成功
		{
			protocol_dump(LOG_HOZON, "PRVT_PROT", pack.packHeader.sign, 18 + 1 + tboxDisBodydataLen + tboxAppdataLen , 1);	
		}	
		timeout = tm_get_time();
	}
	
	return 0;
}

/******************************************************
*函数名：static void PrvtPro_writeout

*形  参：

*返回值：

*描  述：

*备  注：
******************************************************/
static int PrvtPro_writeout(const void *buffer,size_t size,void *key)
{
	int i;
	log_i(LOG_HOZON, "PrvtPro_writeout <<<");
	if(size > PP_MSG_DATA_LEN)
	{
		log_i(LOG_HOZON, "the size of data greater than PP_MSG_DATA_LEN");
		return 0;
	}
	
	switch(*((uint8_t*)key))
	{
		case PP_ENCODE_DISBODY:
		{
			log_i(LOG_HOZON, "PP_ENCODE_DISBODY <<<");
			for(i = 0;i < size;i++)
			{
				tboxDisBodydata[i] = ((unsigned char *)buffer)[i];
			}
			tboxDisBodydataLen = size;
		}
		break;
		case PP_ENCODE_APPDATA:
		{
			log_i(LOG_HOZON, "PP_ENCODE_APPDATA <<<");
			for(i = 0;i < size;i++)
			{
				tboxAppdata[i] = ((unsigned char *)buffer)[i];
			}
			tboxAppdataLen = size;
		}
		break;
		default:
		break;
	}
	return 0;
}
