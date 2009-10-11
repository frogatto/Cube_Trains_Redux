#ifndef COLLISION_UTILS_HPP_INCLUDED
#define COLLISION_UTILS_HPP_INCLUDED

#include "entity_fwd.hpp"
#include "solid_map.hpp"

class entity;
class level;

struct collision_info {
	collision_info() : friction(0), traction(0), damage(0), adjust_y(0), platform(false), area_id(0), collide_with_area_id(0)
	{}
	int friction;
	int traction;
	int damage;
	int adjust_y;
	bool platform;
	const std::string* area_id;

	entity_ptr collide_with;
	const std::string* collide_with_area_id;
};

enum ALLOW_PLATFORM { SOLID_ONLY, SOLID_AND_PLATFORMS };

bool point_standable(const level& lvl, const entity& e, int x, int y, collision_info* info=NULL, ALLOW_PLATFORM allow_platform=SOLID_AND_PLATFORMS);

bool entity_collides(level& lvl, const entity& e, MOVE_DIRECTION dir, collision_info* info=NULL);

bool entity_collides_with_entity(const entity& e, const entity& other,
                                 const std::string** area_id=NULL,
								 const std::string** other_area_id=NULL);

bool entity_collides_with_level(const level& lvl, const entity& e, MOVE_DIRECTION dir, int* friction=NULL, int* traction=NULL, int* damage=NULL);
int entity_collides_with_level_count(const level& lvl, const entity& e, MOVE_DIRECTION dir);

#endif
