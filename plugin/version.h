/* Shared by the plugin and VitaDeck: bump whenever plugin/vdreturn.c changes
   behaviour. The running plugin writes it to PLUGIN_VER_FILE when SceShell loads
   it; VitaDeck compares it with its bundled copy to tell "reboot needed". */
#ifndef VD_PLUGIN_VERSION_H
#define VD_PLUGIN_VERSION_H
#define PLUGIN_VERSION  2          /* 1 = return/autostart, 2 = + playtime */
#define PLUGIN_VER_FILE "ux0:data/VitaDeck/plugin.ver"
#endif
