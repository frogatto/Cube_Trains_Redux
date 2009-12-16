#ifndef water_PARTICLE_SYSTEM_H
#define water_PARTICLE_SYSTEM_H

#include <GL/gl.h>

#include <deque>

#include "particle_system.hpp"
#include "foreach.hpp"
#include "entity.hpp"
#include "wml_utils.hpp"
#include "color_utils.hpp"
#include "geometry.hpp"

struct water_particle_system_info {
	water_particle_system_info(wml::const_node_ptr node)
	: number_of_particles(wml::get_int(node, "number_of_particles", 1500)),
	repeat_period(wml::get_int(node, "repeat_period", 1000)),
	velocity_x(wml::get_int(node, "velocity_x")),
	velocity_y(wml::get_int(node, "velocity_y", -5)),
	velocity_rand(wml::get_int(node, "velocity_rand", 3)),
	line_width(wml::get_int(node, "line_width", 1)),
	line_length(wml::get_int(node, "line_length", 1))
	{
		irgba = graphics::color(node->attr("color")).value();
	}
	int number_of_particles;
	int repeat_period;
	int velocity_x, velocity_y;
	int velocity_rand;
	int line_width, line_length;
	
	union {
		uint8_t rgba[4];
		uint32_t irgba;
	};
};

class water_particle_system_factory : public particle_system_factory {
public:
	explicit water_particle_system_factory(wml::const_node_ptr node);
	~water_particle_system_factory() {}
	
	particle_system_ptr create(const entity& e) const;
	water_particle_system_info info;
};

class water_particle_system : public particle_system
{
public:
	water_particle_system(const entity& e, const water_particle_system_factory& factory);
	
	bool is_destroyed() const { return false; }
	void process(const level& lvl, const entity& e);
	void draw(const rect& area, const entity& e) const;
	
private:
	variant get_value(const std::string& key) const { return variant(); }
	void set_value(const std::string& key, const variant& value);	
	
	const water_particle_system_factory& factory_;
	const water_particle_system_info& info_;
	
	int cycle_;
	
	rect area_;
	
	struct particle {
		GLfloat pos[2];
		GLfloat velocity;
	};
	
	GLfloat direction[2];
	GLfloat base_velocity;
	
	std::vector<particle> particles_;
};

#endif
