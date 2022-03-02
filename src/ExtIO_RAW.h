/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef EXTIO_RAW_H
#define EXTIO_RAW_H

#include <stdint.h>

#define ARRAY_SIZE(arr)		(sizeof(arr) / sizeof((arr)[0]))
#define ARRAY_SSIZE(arr)	((SSIZE_T)ARRAY_SIZE(arr))

#define EXTIO_RAW_NAME		"ExtIO RAW"
#define EXTIO_RAW_ERROR(str)	MessageBox(NULL, TEXT(str), TEXT(EXTIO_RAW_NAME), \
					   MB_OK | MB_ICONERROR)

#define EXTIO_RAW_MINSRATE	8000
#define EXTIO_RAW_MAXSRATE	50000000
#define EXTIO_RAW_MINBUF	2
#define EXTIO_RAW_MAXBUF	128

/* ExtIO HW Type Codes */
#define EXTIO_USBFLOAT32	7

/* ExtIO Status Codes */
#define EXTIO_CHANGED_SR	100
#define EXTIO_CHANGED_LO	101
#define EXTIO_REQ_STOP		108
#define EXTIO_CHANGED_ATT	125
#define EXTIO_CHANGED_RF_IF	136

#define EXTIO_SET_STATUS(EXTIO_CB, EXTIO_CMD)	EXTIO_CB(-1, EXTIO_CMD, 0, NULL)

extern HMODULE hInst;

extern "C" void __stdcall CloseHW(void);
extern "C" long __stdcall GetHWLO(void);
extern "C" long __stdcall GetHWSR(void);
extern "C" int __stdcall GetStatus(void);
extern "C" void __stdcall HideGUI(void);
extern "C" bool __stdcall InitHW(char *name, char *model, int &hwtype);
extern "C" bool __stdcall OpenHW(void);
extern "C" void __stdcall SetCallback(void (*ParentCallback)(int, int, float, void *));
extern "C" long __stdcall SetHWLO(long LOfreq);
extern "C" void __stdcall ShowGUI(void);
extern "C" int __stdcall StartHW(long LOfreq);
extern "C" void __stdcall StopHW(void);
extern "C" void __stdcall SwitchGUI(void);

static inline uint32_t srate_validate(uint32_t srate)
{
	if (srate < EXTIO_RAW_MINSRATE)
		return EXTIO_RAW_MINSRATE;
	if (srate > EXTIO_RAW_MAXSRATE)
		return EXTIO_RAW_MAXSRATE;
	return srate;
}

static inline uint32_t rawbuf_validate(uint32_t rawbuf)
{
	if (rawbuf < EXTIO_RAW_MINBUF)
		return EXTIO_RAW_MINBUF;
	if (rawbuf > EXTIO_RAW_MAXBUF)
		return EXTIO_RAW_MAXBUF;
	return rawbuf;
}

#endif /* EXTIO_RAW_H */
