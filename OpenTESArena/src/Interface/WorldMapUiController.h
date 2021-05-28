#ifndef WORLD_MAP_UI_CONTROLLER_H
#define WORLD_MAP_UI_CONTROLLER_H

class Game;

namespace WorldMapUiController
{
	// Fast travel.
	void onFastTravelAnimationFinished(Game &game, int targetProvinceID, int targetLocationID, int travelDays);
	void onFastTravelCityArrivalPopUpSelected(Game &game);
}

#endif
