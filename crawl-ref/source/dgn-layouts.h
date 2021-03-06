#ifndef DGN_LAYOUTS_H
#define DGN_LAYOUTS_H

#include "enum.h"

void dgn_build_basic_level(int level_number);
void dgn_build_bigger_room_level(void);
void dgn_build_chaotic_city_level(dungeon_feature_type force_wall);
void dgn_build_rooms_level(int nrooms);

#endif
