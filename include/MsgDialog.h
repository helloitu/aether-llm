#ifndef _ORBIS_MSG_H_
#define _ORBIS_MSG_H_

#include <stdint.h>
#include "CommonDialog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIS_MSG_DIALOG_MODE_USER_MSG			(1)
#define ORBIS_MSG_DIALOG_MODE_PROGRESS_BAR		(2)
#define ORBIS_MSG_DIALOG_MODE_SYSTEM_MSG		(3)

#define ORBIS_MSG_DIALOG_BUTTON_TYPE_OK			(0)
#define ORBIS_MSG_DIALOG_BUTTON_TYPE_YESNO		(1)
#define ORBIS_MSG_DIALOG_BUTTON_TYPE_NONE		(2)
#define ORBIS_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL	(3)
#define ORBIS_MSG_DIALOG_BUTTON_TYPE_WAIT		(5)
#define ORBIS_MSG_DIALOG_BUTTON_TYPE_2BUTTONS	(9)

#define ORBIS_MSG_DIALOG_BUTTON_ID_INVALID		(0)
#define ORBIS_MSG_DIALOG_BUTTON_ID_OK			(1)
#define ORBIS_MSG_DIALOG_BUTTON_ID_YES			(1)
#define ORBIS_MSG_DIALOG_BUTTON_ID_NO			(2)

typedef struct OrbisMsgDialogButtonsParam {
	const char *msg1;
	const char *msg2;
	char reserved[32];
} OrbisMsgDialogButtonsParam;

typedef struct OrbisMsgDialogUserMessageParam {
	int32_t buttonType;
	int :32;
	const char *msg;
	OrbisMsgDialogButtonsParam *buttonsParam;
	char reserved[24];
} OrbisMsgDialogUserMessageParam;

typedef struct OrbisMsgDialogSystemMessageParam {
	int32_t sysMsgType;
	char reserved[32];
} OrbisMsgDialogSystemMessageParam;

typedef struct OrbisMsgDialogProgressBarParam {
	int32_t barType;
	int :32;
	const char *msg;
	char reserved[64];
} OrbisMsgDialogProgressBarParam;

typedef struct OrbisMsgDialogParam {
	OrbisCommonDialogBaseParam baseParam;
	size_t size;
	int32_t mode;
	int :32;
	OrbisMsgDialogUserMessageParam *userMsgParam;
	OrbisMsgDialogProgressBarParam *progBarParam;
	OrbisMsgDialogSystemMessageParam *sysMsgParam;
	int32_t userId;
	char reserved[40];
	int :32;
} OrbisMsgDialogParam;

typedef struct OrbisMsgDialogResult {
	int32_t mode;
	int32_t result;
	int32_t buttonId;
	char reserved[32];
} OrbisMsgDialogResult;

int sceMsgDialogInitialize();
int sceMsgDialogTerminate();
int sceMsgDialogUpdateStatus();
int32_t sceMsgDialogOpen(const OrbisMsgDialogParam *param);
int32_t sceMsgDialogGetResult(OrbisMsgDialogResult *result);
int32_t sceMsgDialogProgressBarSetMsg(int target, const char *barMsg);
int32_t sceMsgDialogProgressBarSetValue(int idc, uint32_t rate);

static inline void OrbisMsgDialogParamInitialize(OrbisMsgDialogParam *param)
{
	memset(param, 0x0, sizeof(OrbisMsgDialogParam));
	_sceCommonDialogBaseParamInit(&param->baseParam);
	param->size = sizeof(OrbisMsgDialogParam);
}

#ifdef __cplusplus
}
#endif

#endif
