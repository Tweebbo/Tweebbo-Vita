#pragma once
#include "common.h"
#define PHOTO_W 322
#define PHOTO_H 234
bool client_camera_owned(void);
void client_camera_update(void);
void client_camera_action(int cmd,int arg);
void client_camera_draw(float x,float y,float w,float h);
bool client_camera_capturing(void);
void client_camera_scene(void);
