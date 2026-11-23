#pragma once
namespace net {
void init();   // brings up the WiFi chip; blocks for about a second
void poll();   // call every main-loop iteration
}
