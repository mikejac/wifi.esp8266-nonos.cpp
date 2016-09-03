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

#include "wifi.h"
#include <github.com/mikejac/misc.esp8266-nonos.cpp/espmissingincludes.h>
#include <github.com/mikejac/timer.esp8266-nonos.cpp/timer.h>
#include <github.com/mikejac/date_time.esp8266-nonos.cpp/system_time.h>
#include <osapi.h>
#include <espconn.h>
#include <ip_addr.h>

#define DTXT(...)   os_printf(__VA_ARGS__)

#define CONNECT_CHECK_INTERVAL_SECONDS      15
#define CONNECT_TIMEOUT_SECONDS             30
#define MESH_CHECK_INTERVAL_SECONDS         10

/******************************************************************************************************************
 * local var's
 *
 */

static WIFI_AP*             wifi_list;
static WIFI_AP*             wifi_best_ssid;

typedef struct WIFI 
{
    WIFI_Mode               m_WIFIMode;
    struct station_config   m_StationConfig;
    char                    m_Mac[20];
    struct ip_info          m_Info;
    WIFI_Callback           m_OnConnectCallback;
    WIFI_Callback           m_OnDisconnectCallback;
    void*                   m_CallbackPtr;
} WIFI;

typedef struct WIFIMesh
{
    struct softap_config    m_ApConfig;
} WIFIMesh;

static WIFI     wifi;
static WIFIMesh wifi_mesh;

static Timer connect_check_timer;
static Timer connect_timeout_timer;
static Timer mesh_check_timer;

typedef enum {
    none = 0,
    // wifi        
    wifi_connect,
    wifi_connect_in_progress,
    wifi_connect_done,
    wifi_connect_fail,
    wifi_disconnect,
    wifi_disconnect_in_progress,
    wifi_disconnect_done,
    wifi_scan,
    wifi_scan_in_progress,
    wifi_scan_done,
    wifi_scan_fail,
#if defined(WITH_SMARTLINK)        
    wifi_smartlink,
    wifi_smartlink_scan_in_progress,
    wifi_smartlink_in_progress,
    wifi_smartlink_done,
    wifi_smartlink_fail,
#endif
#if defined(WITH_SMARTWEB)
    wifi_smartweb,
    wifi_smartweb_run,
    wifi_smartweb_in_progress,
    wifi_smartweb_done,
    wifi_smartweb_fail,
#endif
    wifi_disabled,
    // other
    wifi_ready
} WIFI_state_t;

static WIFI_state_t WIFI_state;

typedef enum {
    mesh_none = 0,
    mesh_connect,
    mesh_connect_in_progress,
    mesh_connect_done,
    mesh_connect_fail,
    mesh_scan_in_progress,
    mesh_scan_done,            
    mesh_disabled
} WIFI_Mesh_state_t;

static WIFI_Mesh_state_t WIFI_Mesh_state;

// ssid length = 32
static char mesh_prefix[14];
static char mesh_postfix[16];
static char mesh_status;


#define MESH_STATUS_NONE        '0'
#define MESH_STATUS_CONNECTED   '1'

/******************************************************************************************************************
 * prototypes
 *
 */

/**
 * 
 * @return 
 */
static WIFI_state_t do_wifi_connect(void);
/**
 * 
 * @return 
 */
static WIFI_state_t do_wifi_connect_done(void);
/**
 * 
 * @return 
 */
static WIFI_state_t do_wifi_disconnect(void);
/**
 * 
 * @return 
 */
static WIFI_state_t do_wifi_disconnect_done(void);
/**
 * 
 * @return 
 */
static WIFI_state_t do_wifi_check(void);
/**
 * 
 * @return 
 */
static WIFI_Mesh_state_t do_wifi_mesh_connect(void);
/**
 * 
 * @return 
 */
static WIFI_Mesh_state_t do_wifi_mesh_check(void);
/**
 * 
 * @return 
 */
static WIFI_state_t do_wifi_scan(void);
/**
 * 
 * @return 
 */
static WIFI_state_t do_wifi_scan_done(void);
/**
 * 
 * @param arg
 * @param status
 */
static void scan_done_callback(void* arg, STATUS status);
/**
 * 
 * @param mode
 * @param p1
 * @param p2
 * @return 
 */
//static void mesh_callback(void);
/**
 * 
 * @param status
 * @return 
 */
static int build_mesh_ap_ssid(char status);
/**
 * 
 * @param ssid
 * @return 
 */
static WIFI_AP* wifi_find_ssid(const char* ssid);

/******************************************************************************************************************
 * public functions
 *
 */

/**
 * 
 * @param p1
 * @param p2
 * @return 
 */
int ICACHE_FLASH_ATTR WIFI_Initialize(const void* p1, const void* p2)
{
    DTXT("WIFI_Initialize(): begin\n");
    
    int rc = 0;
    
    WIFI_state                  = none;
    WIFI_Mesh_state             = none;
    wifi.m_OnConnectCallback    = 0;
    wifi.m_OnDisconnectCallback = 0;
    wifi.m_CallbackPtr          = 0;

    wifi.m_StationConfig.ssid[0]     = '\0';
    wifi.m_StationConfig.password[0] = '\0';
    
    wifi_set_opmode(NULL_MODE);
    wifi_station_set_config(&wifi.m_StationConfig);
    wifi_station_set_auto_connect(0);
    
    uint8_t hwaddr[6];
    
    // get our MAC address and convert it to text for future use
    wifi_get_macaddr(STATION_IF, hwaddr);
    os_sprintf(wifi.m_Mac, MACSTR, MAC2STR(hwaddr));
    
    DTXT("WIFI_Initialize(): MAC = %s\n", wifi.m_Mac);
    
    wifi.m_WIFIMode = ap_fixed;
    wifi_best_ssid  = NULL;
    WIFI_state      = wifi_connect;
    WIFI_Mesh_state = mesh_disabled;

    if(p1 != NULL) {
        os_strcpy((char*)(wifi.m_StationConfig.ssid), p1);

        if(p2 != NULL) {
            os_strcpy((char*)(wifi.m_StationConfig.password), p2);
        }
        else {
            wifi.m_StationConfig.password[0] = '\0';
        }
    }
    else {
        wifi.m_StationConfig.ssid[0]     = '\0';
        wifi.m_StationConfig.password[0] = '\0';

        rc = -1;
    }
    
    DTXT("WIFI_Initialize(): end; rc = %d\n", rc);
    
    return rc;
}
/**
 * 
 * @param list
 * @return 
 */
int ICACHE_FLASH_ATTR WIFI_InitializeEx(WIFI_AP list[])
{
    wifi_list = list;
    
    DTXT("WIFI_InitializeEx(): begin\n");
    
    int rc = 0;
    
    wifi.m_OnConnectCallback    = 0;
    wifi.m_OnDisconnectCallback = 0;
    wifi.m_CallbackPtr          = 0;

    wifi.m_StationConfig.ssid[0]     = '\0';
    wifi.m_StationConfig.password[0] = '\0';
    
    wifi_set_opmode(NULL_MODE);
    wifi_station_set_config(&wifi.m_StationConfig);
    wifi_station_set_auto_connect(0);
    
    uint8_t hwaddr[6];
    
    // get our MAC address and convert it to text for future use
    wifi_get_macaddr(STATION_IF, hwaddr);
    os_sprintf(wifi.m_Mac, MACSTR, MAC2STR(hwaddr));
    
    DTXT("WIFI_InitializeEx(): MAC = %s\n", wifi.m_Mac);
    
    wifi.m_WIFIMode = ap_fixed_auto;
    wifi_best_ssid  = NULL;
    WIFI_state      = wifi_scan;
    WIFI_Mesh_state = mesh_disabled;

    DTXT("WIFI_InitializeEx(): end; rc = %d\n", rc);
    
    return rc;
}
/**
 * 
 * @param mode
 * @param ssid
 * @param pass
 * @param prefix
 * @param group
 * @return 
 */
int ICACHE_FLASH_ATTR WIFI_MeshInitialize(WIFI_Mode mode, const void* ssid, const void* pass, const char* prefix, const char* group)
{
    DTXT("WIFI_MeshInitialize(): begin\n");
    
    int rc = 0;

    WIFI_state                  = none;
    WIFI_Mesh_state             = none;
    wifi.m_OnConnectCallback    = 0;
    wifi.m_OnDisconnectCallback = 0;
    wifi.m_CallbackPtr          = 0;

    wifi.m_StationConfig.ssid[0]     = '\0';
    wifi.m_StationConfig.password[0] = '\0';
    
    wifi_set_opmode(NULL_MODE);
    wifi_station_set_config(&wifi.m_StationConfig);
    wifi_station_set_auto_connect(0);
    
    os_strcpy(mesh_prefix, prefix);    
    os_strcpy((char*)(wifi_mesh.m_ApConfig.password), "AbCdE");
    
    uint8_t hwaddr[6];
    
    // get our MAC address and convert it to text for future use
    wifi_get_macaddr(STATION_IF, hwaddr);
    os_sprintf(wifi.m_Mac, MACSTR, MAC2STR(hwaddr));
    
    os_sprintf(mesh_postfix, "%02X%02X%02X%02X%02X%02X", MAC2STR(hwaddr));
    
    DTXT("WIFI_MeshInitialize(): MAC = %s\n", wifi.m_Mac);
    
    mesh_status = MESH_STATUS_NONE;
    
    wifi_mesh.m_ApConfig.ssid_len       = 0;
    wifi_mesh.m_ApConfig.authmode       = AUTH_OPEN;
    wifi_mesh.m_ApConfig.ssid_hidden    = 0;
    wifi_mesh.m_ApConfig.max_connection = 4;

    switch(mode) {
        case ap_fixed:
            DTXT("WIFI_MeshInitialize(): ap_fixed\n");
            
            wifi.m_WIFIMode = ap_fixed;
            WIFI_state      = wifi_connect;
            WIFI_Mesh_state = mesh_disabled;
            
            if(ssid != NULL) {
                os_strcpy((char*)(wifi.m_StationConfig.ssid), ssid);

                if(pass != NULL) {
                    os_strcpy((char*)(wifi.m_StationConfig.password), pass);
                }
                else {
                    wifi.m_StationConfig.password[0] = '\0';
                }
            }
            else {
                wifi.m_StationConfig.ssid[0]     = '\0';
                wifi.m_StationConfig.password[0] = '\0';

                rc = -1;
            }
            break;
            
        case mesh_root:
            DTXT("WIFI_MeshInitialize(): mesh_root\n");
            
            wifi.m_WIFIMode = mesh_root;
            WIFI_state      = wifi_connect;
            WIFI_Mesh_state = none; //mesh_connect;
            
            //wifi_station_set_config(&wifi.m_StationConfig);

            if(ssid != NULL) {
                os_strcpy((char*)(wifi.m_StationConfig.ssid), ssid);

                if(pass != NULL) {
                    os_strcpy((char*)(wifi.m_StationConfig.password), pass);
                }
                else {
                    wifi.m_StationConfig.password[0] = '\0';
                }
            }
            else {
                wifi.m_StationConfig.ssid[0]     = '\0';
                wifi.m_StationConfig.password[0] = '\0';

                rc = -1;
            }
            
            wifi_station_set_config(&wifi.m_StationConfig);

            build_mesh_ap_ssid(mesh_status);
            break;
            
        case mesh_non_leaf:
            DTXT("WIFI_MeshInitialize(): mesh_non_leaf\n");

            wifi.m_WIFIMode = mesh_non_leaf;
            WIFI_state      = wifi_disabled;
            WIFI_Mesh_state = mesh_connect;
            
            wifi.m_StationConfig.ssid[0]     = '\0';
            wifi.m_StationConfig.password[0] = '\0';

            //wifi_station_disconnect();

            wifi_station_set_config(&wifi.m_StationConfig);
            break;
            
        case mesh_leaf:
            DTXT("WIFI_MeshInitialize(): mesh_leaf\n");
            
            wifi.m_WIFIMode = mesh_leaf;
            WIFI_state      = wifi_disabled;
            WIFI_Mesh_state = mesh_connect;
            
            wifi.m_StationConfig.ssid[0]     = '\0';
            wifi.m_StationConfig.password[0] = '\0';
            
            //wifi_station_disconnect();
            
            wifi_station_set_config(&wifi.m_StationConfig);
            break;
            
        default:
            break;
    }

    DTXT("WIFI_MeshInitialize(): end; rc = %d\n", rc);
    
    return rc;
}
/**
 * 
 * @param on_connect
 * @param on_disconnect
 * @param ptr
 * @return 
 */
int ICACHE_FLASH_ATTR WIFI_SetCallback(WIFI_Callback on_connect, WIFI_Callback on_disconnect, void* ptr)
{
    wifi.m_OnConnectCallback    = on_connect;
    wifi.m_OnDisconnectCallback = on_disconnect;
    wifi.m_CallbackPtr          = ptr;
    
    return 0;
}
/**
 * 
 * @return 
 */
int ICACHE_FLASH_ATTR WIFI_IsConnected(void)
{
    switch(wifi.m_WIFIMode) {
        case ap_fixed:
        case ap_fixed_auto:
            //DTXT("WIFI_IsConnected(): ap_fixed\n");
            return (WIFI_state == wifi_ready) ? 1 : 0;
            break;
            
        case mesh_root:
            //DTXT("WIFI_IsConnected(): mesh_root\n");
            //return (espconn_mesh_get_status() == MESH_ONLINE_AVAIL) ? 1 : 0;
            return (WIFI_state == wifi_ready) ? 1 : 0;
            break;
            
        case mesh_non_leaf:
            //DTXT("WIFI_IsConnected(): mesh_non_leaf\n");
            //return (espconn_mesh_get_status() == MESH_ONLINE_AVAIL) ? 1 : 0;
            //break;
            
        case mesh_leaf:
            //DTXT("WIFI_IsConnected(): mesh_leaf\n");
            //return  () && ((espconn_mesh_get_status() == MESH_ONLINE_AVAIL) ? 1 : 0;
            break;
    }
    
    return 0;
}
/**
 * 
 * @return 
 */
int ICACHE_FLASH_ATTR WIFI_connect(void)
{
    WIFI_state = wifi_connect;
    
    return 0;
}
/**
 * 
 * @return 
 */
int ICACHE_FLASH_ATTR WIFI_disconnect(void)
{
    WIFI_state = wifi_disconnect;
    
    return 0;
}
/**
 * 
 * @return 
 */
int ICACHE_FLASH_ATTR WIFI_Run(void)
{
    //DTXT("WIFI_Run(): begin\n");
    
    int rc = 0;
    
    switch(WIFI_state) {
        case wifi_disabled:                                                     // if we're mesh non-leaf or mesh leaf
            break;
            
        case none:
            //WIFI_state = do_wifi_connect();
            //countdown(&timer1, CHECK_INTERVAL_SECONDS);
            break;

        case wifi_connect:
            WIFI_state = do_wifi_connect();
            countdown(&connect_check_timer,   CONNECT_CHECK_INTERVAL_SECONDS);
            countdown(&connect_timeout_timer, CONNECT_TIMEOUT_SECONDS);
            break;

        case wifi_connect_in_progress:
            if(wifi.m_WIFIMode != ap_fixed && expired(&connect_timeout_timer)) {
                DTXT("WIFI_Run(): connect timeout\n");
                
                WIFI_state = wifi_disabled;
            }
            else if(expired(&connect_check_timer)) {
                WIFI_state = do_wifi_check();
                
                countdown(&connect_check_timer, CONNECT_CHECK_INTERVAL_SECONDS);
            }
            break;

        case wifi_connect_fail:
            if(wifi.m_WIFIMode != ap_fixed) {
                DTXT("WIFI_Run(): connect fail\n");
                
                WIFI_state = wifi_disabled;
            }
            else {
                WIFI_state = do_wifi_connect();                                 // start again
                countdown(&connect_check_timer, CONNECT_CHECK_INTERVAL_SECONDS);
            }
            break;

        case wifi_connect_done:
            WIFI_state = do_wifi_connect_done();
            
            if(wifi.m_OnConnectCallback != 0) {
                wifi.m_OnConnectCallback(1, wifi.m_CallbackPtr);                // notify user
            }
            
            countdown(&connect_check_timer, CONNECT_CHECK_INTERVAL_SECONDS);
            break;
            
        case wifi_disconnect:
            WIFI_state = do_wifi_disconnect();
            break;
            
        case wifi_disconnect_in_progress:
            break;
            
        case wifi_disconnect_done:
            WIFI_state = do_wifi_disconnect_done();
            
            if(wifi.m_OnDisconnectCallback != 0) {
                wifi.m_OnDisconnectCallback(1, wifi.m_CallbackPtr);             // notify user
            }
            break;
            
        case wifi_scan:
            WIFI_state = do_wifi_scan();
            break;

        case wifi_scan_in_progress:
            break;

        case wifi_scan_done:
            WIFI_state = do_wifi_scan_done();
            break;

        case wifi_scan_fail:
            if(expired(&connect_check_timer)) {
                WIFI_state = do_wifi_scan();
                
                countdown(&connect_check_timer, CONNECT_CHECK_INTERVAL_SECONDS);
            }
            break;
            
        case wifi_ready:
            if(expired(&connect_check_timer)) {
                WIFI_state_t state = do_wifi_check();
                
                if(state != wifi_connect_done) {
                    if(wifi.m_WIFIMode == ap_fixed_auto) {
                        WIFI_state = wifi_scan;
                    }
                    else {
                        WIFI_state = state;                                     // something happened
                    }
                    //if(wifi.m_Callback != 0) {
                    //    wifi.m_Callback(0, wifi.m_CallbackPtr);                 // notify user
                    //}
                }
                
                countdown(&connect_check_timer, CONNECT_CHECK_INTERVAL_SECONDS);
            }
            break;
            
        default:
            DTXT("WIFI_Run(): WIFI_state default\n");
            break;
    }
    
    switch(WIFI_Mesh_state) {
        case mesh_disabled:
            break;
            
        case none:
            if(WIFI_state == wifi_disabled) {
                DTXT("WIFI_Run(): mesh - wifi disabled, start mesh connect\n");
                WIFI_Mesh_state = mesh_connect;
            }
            break;
            
        case mesh_connect:
            WIFI_Mesh_state = do_wifi_mesh_connect();
            countdown(&mesh_check_timer, MESH_CHECK_INTERVAL_SECONDS);
            break;
            
        case mesh_scan_in_progress:
            break;
            
        case mesh_scan_done:
            WIFI_Mesh_state = do_wifi_mesh_connect();
            break;
            
        case mesh_connect_in_progress:
            if(expired(&mesh_check_timer)) {
                WIFI_Mesh_state = do_wifi_mesh_check();
                
                countdown(&mesh_check_timer, MESH_CHECK_INTERVAL_SECONDS);
            }
            break;
            
        case mesh_connect_done:
            break;
            
        case mesh_connect_fail:
            break;
            
        default:
            DTXT("WIFI_Run(): WIFI_Mesh_state default\n");
            break;
    }
    
    /*switch(wifi.m_WIFIMode) {
        case ap_fixed:
            break;
            
        case mesh_root:
            break;
            
        case mesh_non_leaf:
            break;
            
        case mesh_leaf:
            break;
    }*/
    
    //DTXT("WIFI_Run(): end\n");
    
    return rc;
}
/**
 * 
 * @return 
 */
const char* WIFI_GetMAC(void)
{
    return wifi.m_Mac;
}

/******************************************************************************************************************
 * private functions
 *
 */

/**
 * 
 * @return 
 */
static WIFI_state_t ICACHE_FLASH_ATTR do_wifi_connect(void)
{
    DTXT("do_wifi_connect(): begin\n");
    
    switch(wifi.m_WIFIMode) {
        case ap_fixed:
        case ap_fixed_auto:
            // required to call wifi_set_opmode before station_set_config
            wifi_set_opmode_current(STATION_MODE);

            wifi_station_set_config_current(&wifi.m_StationConfig);
            wifi_station_connect();

            wifi_station_set_auto_connect(1);
            break;
            
        case mesh_root:
            // required to call wifi_set_opmode before station_set_config
            wifi_set_opmode_current(STATION_MODE);

            wifi_station_set_config_current(&wifi.m_StationConfig);
            wifi_station_connect();

            wifi_station_set_auto_connect(1);
            break;
            
        case mesh_non_leaf:
            break;
            
        case mesh_leaf:
            break;
    }
    
    DTXT("do_wifi_connect(): end\n");
    
    return wifi_connect_in_progress;
}
/**
 * 
 * @return 
 */
static WIFI_state_t ICACHE_FLASH_ATTR do_wifi_connect_done(void)
{
    DTXT("do_wifi_connect_done(): begin\n");
    
    WIFI_state_t state = wifi_ready;
    
    wifi_get_ip_info(STATION_IF, &(wifi.m_Info));
    
    DTXT("do_wifi_connect_done(): ip = %d.%d.%d.%d\n", ip4_addr1(&wifi.m_Info.ip), ip4_addr2(&wifi.m_Info.ip), ip4_addr3(&wifi.m_Info.ip), ip4_addr4(&wifi.m_Info.ip));
    
    switch(wifi.m_WIFIMode) {
        case ap_fixed:
            DTXT("do_wifi_connect_done(): ap_fixed\n");
            break;
            
        case ap_fixed_auto:
            DTXT("do_wifi_connect_done(): ap_fixed_auto\n");
            break;
            
        case mesh_root:
            DTXT("do_wifi_connect_done(): mesh_root\n");
            wifi_set_opmode_current(STATIONAP_MODE);
            
            mesh_status = MESH_STATUS_CONNECTED;

            build_mesh_ap_ssid(mesh_status);
        
            wifi_softap_set_config_current(&wifi_mesh.m_ApConfig);

            //setup_udp();
            break;
            
        case mesh_non_leaf:
            DTXT("do_wifi_connect_done(): mesh_non_leaf\n");
            break;
            
        case mesh_leaf:
            DTXT("do_wifi_connect_done(): mesh_leaf\n");
            break;
    }
                
    DTXT("do_wifi_connect_done(): end\n");
    
    return state;
}
/**
 * 
 * @return 
 */
static WIFI_state_t ICACHE_FLASH_ATTR do_wifi_disconnect(void)
{
    DTXT("do_wifi_disconnect(): begin\n");
    
    switch(wifi.m_WIFIMode) {
        case ap_fixed:
            wifi_station_set_auto_connect(0);
            wifi_station_disconnect();
            break;
            
        case ap_fixed_auto:
            wifi_station_set_auto_connect(0);
            wifi_station_disconnect();
            break;
            
        case mesh_root:
            wifi_station_set_auto_connect(0);
            wifi_station_disconnect();

            mesh_status = MESH_STATUS_NONE;

            build_mesh_ap_ssid(mesh_status);
            
            wifi_softap_set_config(&wifi_mesh.m_ApConfig);
            break;
            
        case mesh_non_leaf:
            break;
            
        case mesh_leaf:
            break;
    }
    
    DTXT("do_wifi_disconnect(): end\n");
    
    return wifi_disconnect_in_progress;
}
/**
 * 
 * @return 
 */
static WIFI_state_t ICACHE_FLASH_ATTR do_wifi_disconnect_done(void)
{
    DTXT("do_wifi_disconnect_done(): begin\n");
    
    WIFI_state_t state = wifi_disabled;
    
    DTXT("do_wifi_disconnect_done(): end\n");
    
    return state;
}
/**
 * 
 * @return 
 */
static WIFI_state_t ICACHE_FLASH_ATTR do_wifi_check(void)
{
    WIFI_state_t state;
    
    uint8_t wifi_status = wifi_station_get_connect_status();
    
    switch(wifi_status) {
        case STATION_IDLE:
            DTXT("do_wifi_check(): STATION_IDLE\n");
            state = wifi_connect_fail;
            break;
            
        case STATION_GOT_IP:
            //DTXT("do_wifi_check(): STATION_GOT_IP\n");
            state = wifi_connect_done;
            break;
            
        case STATION_WRONG_PASSWORD:
            DTXT("do_wifi_check(): STATION_WRONG_PASSWORD\n");
            state = wifi_connect_fail;
            break;

        case STATION_NO_AP_FOUND:
            DTXT("do_wifi_check(): STATION_NO_AP_FOUND\n");
            state = wifi_connect_fail;
            break;

        case STATION_CONNECT_FAIL:
            DTXT("do_wifi_check(): STATION_CONNECT_FAIL\n");
            state = wifi_connect_fail;
            break;
        
        default:
            DTXT("do_wifi_check(): wifi not connected; wifi_status = %d\n", wifi_status);
            state = wifi_connect_fail;
            break;
    }
                
    return state;
}
/**
 * 
 * @return 
 */
static WIFI_Mesh_state_t ICACHE_FLASH_ATTR do_wifi_mesh_connect(void)
{
    DTXT("do_wifi_mesh_connect(): begin\n");

    wifi_station_set_auto_connect(0);
    wifi_station_disconnect();
    
    // start scan
    wifi_station_scan(NULL, &scan_done_callback);
    
    DTXT("do_wifi_mesh_connect(): end\n");
    
    return mesh_scan_in_progress;
}
/**
 * 
 * @return 
 */
static WIFI_state_t ICACHE_FLASH_ATTR do_wifi_scan(void)
{
    DTXT("do_wifi_scan(): begin\n");
    
    // ensure we are in station mode
    wifi_set_opmode(STATION_MODE);
   
    bool rc = wifi_station_scan(NULL, scan_done_callback);
    
    DTXT("do_wifi_scan(): end; rc = %s\n", rc ? "True" : "False");
    
    return wifi_scan_in_progress;
}
/**
 * 
 * @return 
 */
static WIFI_state_t ICACHE_FLASH_ATTR do_wifi_scan_done(void)
{
    DTXT("do_wifi_scan_done(): begin\n");

    WIFI_state_t state;
    
    if(wifi_best_ssid != NULL) {
        os_strcpy((char*)(wifi.m_StationConfig.ssid), wifi_best_ssid->ssid);

        if(wifi_best_ssid->psw != NULL) {
            os_strcpy((char*)(wifi.m_StationConfig.password), wifi_best_ssid->psw);
        }
        else {
            wifi.m_StationConfig.password[0] = '\0';
        }
        
        state = wifi_connect;
    }
    else {
        state = wifi_scan;
    }
    
    DTXT("do_wifi_scan_done(): end\n");
    
    return state;
}
/**
 * 
 * @param arg
 * @param status
 */
static void ICACHE_FLASH_ATTR scan_done_callback(void* arg, STATUS status)
{
    DTXT("scan_done_callback(): begin\n");

    struct bss_info *bss = arg;
    
    sint8    best_rssi = -127;
    WIFI_AP* s;
    
    switch(status) {
        case OK:
            DTXT("scan_done_callback(): status == OK\n");
            
            while(bss) {
                DTXT("%s %d %d %d\n", bss->ssid, bss->channel, bss->rssi, bss->authmode);
                
                s = wifi_find_ssid((const char*)bss->ssid);
                
                if(s != NULL) {
                    if(bss->rssi > best_rssi) {
                        best_rssi      = bss->rssi;
                        wifi_best_ssid = s;
                    }
                }
                
                //DTXT("bssid: %02x:%02x:%02x:%02x:%02x:%02x\n", MAC2STR(inf->bssid));
                //DTXT("ssid:  %s\n", (char*)inf->ssid);
                //DTXT("rssi:  %d\n", inf->rssi);
                
                    //os()->jsonRpc()->getJEncoder()->setName("bssid");   os()->jsonRpc()->getJEncoder()->setString(b);
                    //os()->jsonRpc()->getJEncoder()->setName("ssid");    os()->jsonRpc()->getJEncoder()->setString((char*)inf->ssid);
                    //os()->jsonRpc()->getJEncoder()->setName("channel"); os()->jsonRpc()->getJEncoder()->setInt(inf->channel);
                    //os()->jsonRpc()->getJEncoder()->setName("rssi");    os()->jsonRpc()->getJEncoder()->setInt(inf->rssi);
                    //os()->jsonRpc()->getJEncoder()->setName("auth");    os()->jsonRpc()->getJEncoder()->setString(auth_to_string(b, inf->authmode));                            

                bss = bss->next.stqe_next;
            }
            
            WIFI_state = wifi_scan_done;
            break;
            
        case FAIL:
        case PENDING:
        case BUSY:
        case CANCEL:
            DTXT("scan_done_callback(): status = %d\n", status);
            WIFI_state = wifi_scan_fail;
            break;
    }

    if (WIFI_Mesh_state != mesh_disabled) {
        WIFI_Mesh_state = mesh_scan_done;
    }
    
    DTXT("scan_done_callback(): end\n");    
}
/**
 * 
 * @return 
 */
static WIFI_Mesh_state_t ICACHE_FLASH_ATTR do_wifi_mesh_check(void)
{
    uint8 stationCount = wifi_softap_get_station_num();
    
    DTXT("do_wifi_mesh_check(): stationCount = %d\n", stationCount);
    
    struct station_info *stationInfo = wifi_softap_get_station_info();
    
    if(stationInfo != NULL) {
        while(stationInfo != NULL) {
           DTXT("do_wifi_mesh_check(): station IP: %d.%d.%d.%d\n", IP2STR(&(stationInfo->ip)));
           stationInfo = STAILQ_NEXT(stationInfo, next);
        }
        
        wifi_softap_free_station_info();
    }
    
    //broadcast_udp();
    
    return WIFI_Mesh_state;
    
    if(WIFI_IsConnected() == 1) {
        return mesh_connect_done;

    }
    else {
        return mesh_connect_in_progress;
    }
}
/**
 * 
 * @param status
 * @return 
 */
int ICACHE_FLASH_ATTR build_mesh_ap_ssid(char status)
{
    char buf[2];
    buf[0] = status;
    buf[1] = '\0';

    os_strcpy((char*)(wifi_mesh.m_ApConfig.ssid), mesh_prefix);
    os_strcat((char*)(wifi_mesh.m_ApConfig.ssid), "_");
    os_strcat((char*)(wifi_mesh.m_ApConfig.ssid), buf);
    os_strcat((char*)(wifi_mesh.m_ApConfig.ssid), "_");
    os_strcat((char*)(wifi_mesh.m_ApConfig.ssid), mesh_postfix);
    
    DTXT("build_mesh_ap_ssid(): %s\n", wifi_mesh.m_ApConfig.ssid);
    
    return 0;
}
/**
 * 
 * @param ssid
 * @return 
 */
WIFI_AP* ICACHE_FLASH_ATTR wifi_find_ssid(const char* ssid)
{
    DTXT("wifi_find_ssid(): ssid = %s\n", ssid);
    
    WIFI_AP* p = wifi_list;
    
    while(p->ssid != NULL) {
        DTXT("wifi_find_ssid(): p->ssid = %s\n", p->ssid);
        
        if(os_strcmp(p->ssid, ssid) == 0) {
            return p;
        }
        
        p++;
    }
    
    return NULL;
}