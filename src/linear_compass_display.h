#ifndef LINEAR_COMPASS_DISPLAY_H
#define LINEAR_COMPASS_DISPLAY_H

#include <Arduino.h>

void initCompass();
void stopLinearCompassDisplay();
void setLinearCompassInertia(float inertia);
void updateLinearCompassBearings(float bearing, float targetBearing, float homeBearing);

#endif
