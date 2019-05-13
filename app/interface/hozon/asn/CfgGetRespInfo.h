/*
 * Generated by asn1c-0.9.28 (http://lionet.info/asn1c)
 * From ASN.1 module "HOZON"
 * 	found in "HOZON_PRIV_v1.0.asn"
 * 	`asn1c -gen-PER`
 */

#ifndef	_CfgGetRespInfo_H_
#define	_CfgGetRespInfo_H_


#include <asn_application.h>

/* Including external dependencies */
#include <BOOLEAN.h>
#include <asn_SEQUENCE_OF.h>
#include <constr_SEQUENCE_OF.h>
#include <constr_SEQUENCE.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct FICMConfigSettings;

/* CfgGetRespInfo */
typedef struct CfgGetRespInfo {
	BOOLEAN_t	 result;
	struct ficmCfg {
		A_SEQUENCE_OF(struct FICMConfigSettings) list;
		
		/* Context for parsing across buffer boundaries */
		asn_struct_ctx_t _asn_ctx;
	} *ficmCfg;
	
	/* Context for parsing across buffer boundaries */
	asn_struct_ctx_t _asn_ctx;
} CfgGetRespInfo_t;

/* Implementation */
extern asn_TYPE_descriptor_t asn_DEF_CfgGetRespInfo;

#ifdef __cplusplus
}
#endif

/* Referred external types */
#include "FICMConfigSettings.h"

#endif	/* _CfgGetRespInfo_H_ */
#include <asn_internal.h>