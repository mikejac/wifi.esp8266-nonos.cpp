/* 
 * The MIT License (MIT)
 * 
 * ESP8266 Non-OS Firmware
 * Copyright (c) 2015 Michael Jacobsen (github.com/mikejac)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef WIFI_H
#define	WIFI_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <user_interface.h>

typedef void (*WIFI_Callback)(uint8_t, void*);

typedef enum {
    ap_fixed,
    ap_fixed_auto,
    mesh_root,
    mesh_non_leaf,
    mesh_leaf
} WIFI_Mode;

typedef struct {
    const char*    ssid;
    const char*    psw;
} WIFI_AP;

/**
 * 
 * @param p1
 * @param p2
 * @return 
 */
int WIFI_Initialize(const void* p1, const void* p2);
/**
 * 
 * @param list
 * @return 
 */
int WIFI_InitializeEx(WIFI_AP list[]);
/**
 * 
 * @param mode
 * @param ssid
 * @param pass
 * @param prefix
 * @param group
 * @return 
 */
int WIFI_MeshInitialize(WIFI_Mode mode, const void* ssid, const void* pass, const char* prefix, const char* group);
/**
 * 
 * @param on_connect
 * @param on_disconnect
 * @param ptr
 * @return 
 */
int WIFI_SetCallback(WIFI_Callback on_connect, WIFI_Callback on_disconnect, void* ptr);
/**
 * 
 * @return 
 */
int WIFI_IsConnected(void);
/**
 * 
 * @return 
 */
int WIFI_Run(void);
/**
 * 
 * @return 
 */
const char* WIFI_GetMAC(void);
/**
 * 
 * @return 
 */
int WIFI_connect(void);
/**
 * 
 * @return 
 */
int WIFI_disconnect(void);

#ifdef	__cplusplus
}
#endif

#endif	/* WIFI_H */

