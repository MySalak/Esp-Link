#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>

void web_server_init(void);
void web_server_process(void);
void broadcast_uart(const String& msg);

#endif // WEB_SERVER_H
