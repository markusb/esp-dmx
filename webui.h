/*
 * ESP-DMX web ui
 * 
 * Webinterface related files
 * 
 */

#ifndef _WEBUI_H_
#define _WEBUI_H_

void defaultConfig(void);
bool loadConfig(void);
bool saveConfig(void);
void http_index();
void http_config();
void http_restart();
void http_update();
void http_favicon();
void http_dmx512png();
void http_error404(void);
void ota_restart(void);
void ota_upload(void);

#endif // _WEBUI_H_
