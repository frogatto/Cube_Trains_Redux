#include <boost/bind.hpp>
#include <boost/function.hpp>

#include <cassert>
#include <iostream>

#include "asserts.hpp"
#include "collision_utils.hpp"
#include "custom_object.hpp"
#include "custom_object_functions.hpp"
#include "draw_scene.hpp"
#include "font.hpp"
#include "formatter.hpp"
#include "formula_callable.hpp"
#include "graphical_font.hpp"
#include "level.hpp"
#include "level_logic.hpp"
#include "playable_custom_object.hpp"
#include "raster.hpp"
#include "wml_node.hpp"
#include "wml_utils.hpp"
#include "unit_test.hpp"
#include "utils.hpp"

struct custom_object_text {
	std::string text;
	const_graphical_font_ptr font;
};

custom_object::custom_object(wml::const_node_ptr node)
  : entity(node),
    previous_y_(y()),
	custom_type_(node->get_child("type")),
    type_(custom_type_ ?
	      const_custom_object_type_ptr(new custom_object_type(custom_type_)) :
		  custom_object_type::get(node->attr("type"))),
    frame_(&type_->default_frame()),
	frame_name_(wml::get_str(node, "current_frame", "normal")),
	time_in_frame_(wml::get_int(node, "time_in_frame")),
	velocity_x_(wml::get_int(node, "velocity_x")),
	velocity_y_(wml::get_int(node, "velocity_y")),
	accel_x_(wml::get_int(node, "accel_x")),
	accel_y_(wml::get_int(node, "accel_y")),
	rotate_(0), zorder_(wml::get_int(node, "zorder", type_->zorder())),
	hitpoints_(wml::get_int(node, "hitpoints", type_->hitpoints())),
	was_underwater_(false), invincible_(0),
	lvl_(NULL),
	vars_(new game_logic::map_formula_callable(node->get_child("vars"))),
	tmp_vars_(new game_logic::map_formula_callable),
	tags_(new game_logic::map_formula_callable(node->get_child("tags"))),
	last_hit_by_anim_(0),
	current_animation_id_(0),
	cycle_(wml::get_int(node, "cycle")),
	loaded_(false),
	standing_on_prev_x_(INT_MIN), standing_on_prev_y_(INT_MIN),
	can_interact_with_(false), fall_through_platforms_(0)
{
	wml::const_node_ptr tags_node = node->get_child("tags");
	if(tags_node) {
		tags_ = new game_logic::map_formula_callable(node->get_child("tags"));
	} else {
		tags_ = new game_logic::map_formula_callable;
		foreach(const std::string& tag, type_->tags()) {
			tags_->add(tag, variant(1));
		}
	}

	for(std::map<std::string, variant>::const_iterator i = type_->variables().begin(); i != type_->variables().end(); ++i) {
		if(!vars_->contains(i->first)) {
			vars_->add(i->first, i->second);
		}
	}

	if(node->has_attr("draw_color")) {
		draw_color_.reset(new graphics::color_transform(node->attr("draw_color")));
	}

	if(node->has_attr("label")) {
		set_label(node->attr("label"));
	} else {
		set_distinct_label();
	}

	if(!type_->respawns()) {
		set_respawn(false);
	}

	assert(type_.get());
	set_frame(frame_name_);

	next_animation_formula_ = type_->next_animation_formula();

	custom_object_type::init_event_handlers(node, event_handlers_);

	can_interact_with_ = (event_handlers_.count("interact") || type_->get_event_handler("interact"));

	wml::const_node_ptr editor_info = node->get_child("editor_info");
	if(editor_info) {
		std::cerr << "CREATE EDITOR INFO\n";
		set_editor_info(const_editor_entity_info_ptr(new editor_entity_info(editor_info)));
	}

	wml::const_node_ptr text_node = node->get_child("text");
	if(text_node) {
		text_.reset(new custom_object_text);
		text_->text = text_node->attr("text");
		text_->font = graphical_font::get(text_node->attr("font"));
	}
}

custom_object::custom_object(const std::string& type, int x, int y, bool face_right)
  : entity(x, y, face_right),
    previous_y_(y),
    type_(custom_object_type::get(type)),
	frame_(&type_->default_frame()),
    frame_name_("normal"),
	time_in_frame_(0),
	velocity_x_(0), velocity_y_(0),
	accel_x_(0), accel_y_(0),
	rotate_(0), zorder_(type_->zorder()),
	hitpoints_(type_->hitpoints()),
	was_underwater_(false), invincible_(0),
	lvl_(NULL),
	vars_(new game_logic::map_formula_callable),
	tmp_vars_(new game_logic::map_formula_callable),
	last_hit_by_anim_(0),
	cycle_(0),
	loaded_(false), fall_through_platforms_(0)
{
	for(std::map<std::string, variant>::const_iterator i = type_->variables().begin(); i != type_->variables().end(); ++i) {
		if(!vars_->contains(i->first)) {
			vars_->add(i->first, i->second);
		}
	}

	{
		//generate a random label for the object
		char buf[64];
		sprintf(buf, "_%x", rand());
		set_label(buf);
	}

	assert(type_.get());
	set_frame(frame_name_);

	next_animation_formula_ = type_->next_animation_formula();
}

custom_object::~custom_object()
{
}

wml::node_ptr custom_object::write() const
{
	wml::node_ptr res(new wml::node("character"));
	if(draw_color_) {
		res->set_attr("draw_color", draw_color_->to_string());
	}

	if(label().empty() == false) {
		res->set_attr("label", label());
	}

	if(cycle_ > 1) {
		res->set_attr("cycle", formatter() << cycle_);
	}

	if(frame_name_ != "default") {
		res->set_attr("current_frame", frame_name_);
	}

	res->set_attr("custom", "yes");
	res->set_attr("type", type_->id());
	res->set_attr("x", formatter() << x());
	res->set_attr("y", formatter() << y());
	res->set_attr("velocity_x", formatter() << velocity_x_);
	res->set_attr("velocity_y", formatter() << velocity_y_);

	if(zorder_ != type_->zorder()) {
		res->set_attr("zorder", formatter() << y());
	}

	res->set_attr("face_right", face_right() ? "yes" : "no");
	if(upside_down()) {
		res->set_attr("upside_down", "yes");
	}

	res->set_attr("time_in_frame", formatter() << time_in_frame_);

	if(group() >= 0) {
		res->set_attr("group", formatter() << group());
	}

	for(std::map<std::string, game_logic::const_formula_ptr>::const_iterator i = event_handlers_.begin(); i != event_handlers_.end(); ++i) {
		if(!i->second) {
			continue;
		}
		res->set_attr("on_" + i->first, i->second->str());
	}

	wml::node_ptr vars(new wml::node("vars"));
	vars_->write(vars);
	res->add_child(vars);

	wml::node_ptr tags(new wml::node("tags"));
	tags_->write(tags);
	res->add_child(tags);

	if(custom_type_) {
		res->add_child(wml::deep_copy(custom_type_));
	}

	if(editor_info()) {
		res->add_child(editor_info()->write());
	}

	if(text_) {
		wml::node_ptr node(new wml::node("text"));
		node->set_attr("text", text_->text);
		if(text_->font) {
			node->set_attr("font", text_->font->id());
		}

		res->add_child(node);
	}
	
	return res;
}

void custom_object::setup_drawing() const
{
	if(distortion_) {
		graphics::add_raster_distortion(distortion_.get());
	}
}

void custom_object::draw() const
{
	if(frame_ == NULL) {
		return;
	}

	if(is_human() && ((invincible_/5)%2) == 1) {
		return;
	}

	if(driver_) {
		driver_->draw();
	}

	if(draw_color_) {
		draw_color_->to_color().set_as_current_color();
	}

	frame_->draw(x(), y(), face_right(), upside_down(), time_in_frame_, rotate_);

	if(blur_) {
		blur_->draw();
	}

	if(draw_color_) {
		if(!draw_color_->fits_in_color()) {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			graphics::color_transform transform = *draw_color_;
			while(!transform.fits_in_color()) {
				transform = transform - transform.to_color();
				transform.to_color().set_as_current_color();
				frame_->draw(x(), y(), face_right(), upside_down(), time_in_frame_, rotate_);
			}

			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		static const uint8_t AllWhite[4] = {0xFF, 0xFF, 0xFF, 0xFF};
		glColor4ubv(AllWhite);
	}

//	if(draw_color_int_ != DefaultColor) {
//		static const uint8_t AllWhite[4] = {0xFF, 0xFF, 0xFF, 0xFF};
//		glColor4ubv(AllWhite);
//	}

	draw_debug_rects();

	for(std::map<std::string, particle_system_ptr>::const_iterator i = particle_systems_.begin(); i != particle_systems_.end(); ++i) {
		i->second->draw(rect(last_draw_position().x/100, last_draw_position().y/100, graphics::screen_width(), graphics::screen_height()), *this);
	}

	if(text_ && text_->font) {
		text_->font->draw(x(), y(), text_->text);
	}
}

void custom_object::draw_group() const
{
	if(label().empty() == false && label()[0] != '_') {
		blit_texture(font::render_text(label(), graphics::color_yellow(), 32), x(), y() + 26);
	}

	if(group() >= 0) {
		blit_texture(font::render_text(formatter() << group(), graphics::color_yellow(), 24), x(), y());
	}
}

namespace {
class collide_with_callable : public game_logic::formula_callable {
	entity* e_;
public:
	explicit collide_with_callable(entity* e) : game_logic::formula_callable(false), e_(e) {}
	variant get_value(const std::string& key) const {
		if(key == "collide_with") {
			return variant(e_);
		} else {
			return variant();
		}
	}
};
}

void custom_object::process(level& lvl)
{
	if(type_->use_image_for_collisions()) {
		//anything that uses their image for collisions is a static,
		//un-moving object that will stay immobile.
		return;
	}

	entity::process(lvl);

	//the object should never be colliding with the level at the start of processing.
	assert(!entity_collides_with_level(lvl, *this, MOVE_NONE));

	collision_info stand_info;
	const bool started_standing = is_standing(lvl, &stand_info);

	if(y() > lvl.boundaries().y2()) {
		--hitpoints_;
	}
	
	previous_y_ = y();
	if((started_standing || standing_on_) && velocity_y_ > 0) {
		velocity_y_ = 0;
	}

	lvl_ = &lvl;

	const int start_x = x();
	const int start_y = y();
	++cycle_;

	if(invincible_) {
		--invincible_;
	}

	if(!loaded_) {
		handle_event("load");
		loaded_ = true;
	}

	if(cycle_ == 1) {
		handle_event("create");
		handle_event("done_create");
	}

	variant scheduled_command = get_scheduled_command(lvl.cycle());
	while(!scheduled_command.is_null()) {
		execute_command(scheduled_command);
		scheduled_command = get_scheduled_command(lvl.cycle());
	}

	++time_in_frame_;

	if(stand_info.damage) {
		handle_event("surface_damage");
	}

	if(time_in_frame_ == frame_->duration()) {
		if(next_animation_formula_) {
			variant var = next_animation_formula_->execute(*this);
			set_frame(var.as_string());
		}

		handle_event("end_" + frame_name_ + "_anim");
		handle_event("end_anim");
	}

	const std::string* event = frame_->get_event(time_in_frame_);
	if(event) {
		handle_event(*event);
	}

	const int traction_from_surface = (stand_info.traction*type_->traction())/1000;
	velocity_x_ += (accel_x_ * (traction_from_surface + type_->traction_in_air()) * (face_right() ? 1 : -1))/1000;
	if(!standing_on_ || accel_y_ < 0) {
		//do not accelerate downwards if standing on something.
		velocity_y_ += accel_y_;
	}

	if(type_->friction()) {
		const bool is_underwater = lvl.is_underwater(body_rect());

		const int air_resistance = is_underwater ? lvl.water_resistance() : lvl.air_resistance();

		const int friction = ((stand_info.friction + air_resistance)*type_->friction())/1000;
		const int vertical_resistance = (air_resistance*type_->friction())/1000;
		velocity_x_ = (velocity_x_*(1000 - friction))/1000;
		velocity_y_ = (velocity_y_*(1000 - vertical_resistance))/1000;
	}

	if(type_->affected_by_currents()) {
		lvl.get_current(*this, &velocity_x_, &velocity_y_);
	}

	bool collide = false;

	if(type_->ignore_collide()) {
		move_centipixels(velocity_x_, velocity_y_);
	}

	//calculate velocity which takes into account velocity of the object we're standing on.
	int effective_velocity_x = velocity_x_;
	int effective_velocity_y = velocity_y_;

	if(standing_on_) {
		effective_velocity_x += (standing_on_->feet_x() - standing_on_prev_x_)*100;
		effective_velocity_y += (standing_on_->feet_y() - standing_on_prev_y_)*100;
	}

	if(stand_info.collide_with != standing_on_ && stand_info.adjust_y) {
		//if we're landing on a new platform, we might have to adjust our
		//y position to suit its last movement and put us on top of
		//the platform.
		effective_velocity_y -= stand_info.adjust_y*100;
	}

	collision_info collide_info;
	collision_info jump_on_info;

	//std::cerr << "velocity_y: " << velocity_y_ << "\n";
	collide = false;
	for(int n = 0; n <= std::abs(effective_velocity_y/100) && !collide && !type_->ignore_collide(); ++n) {
		const int dir = effective_velocity_y/100 > 0 ? 1 : -1;
		int damage = 0;

		if(type_->object_level_collisions() && non_solid_entity_collides_with_level(lvl, *this)) {
			handle_event("collide_level");
		}

		if(effective_velocity_y > 0) {
			if(entity_collides(lvl, *this, MOVE_DOWN, &collide_info)) {
				//our 'legs' but not our feet collide with the level. Try to
				//move one pixel to the left or right and see if either
				//direction makes us no longer colliding.
				set_pos(x() + 1, y());
				if(entity_collides(lvl, *this, MOVE_DOWN) || entity_collides(lvl, *this, MOVE_RIGHT)) {
					set_pos(x() - 2, y());
					if(entity_collides(lvl, *this, MOVE_DOWN) || entity_collides(lvl, *this, MOVE_LEFT)) {
						//moving in either direction fails to resolve the collision.
						//This effectively means the object is 'stuck' in a small
						//pit.
						set_pos(x() + 1, y()-1);
						collide = true;
					}
				}
				

			}
		} else {
			//effective_velocity_y < 0 -- going up
			if(entity_collides(lvl, *this, MOVE_UP, &collide_info)) {
				collide = true;
				set_pos(x(), y()+1);
			}
		}

		if(!collide && !type_->ignore_collide() && effective_velocity_y > 0 && is_standing(lvl, &jump_on_info)) {
			if(!jump_on_info.collide_with || jump_on_info.collide_with != standing_on_) {
				collide = true;
			}

			break;
		}
/*
		if((!collide || jump_on_info.collide_with) && !type_->ignore_collide() && velocity_y_ > 0) {
			entity_ptr bounce = lvl.collide(feet_x() - type_->feet_width(), feet_y(), this);
			if(!bounce) {
				bounce = lvl.collide(feet_x() + type_->feet_width(), feet_y(), this);
			}

			if(bounce && bounce->spring_off_head(*this)) {
				tmp_vars_->add("bounce_off", variant(bounce.get()));
				handle_event("bounce");
				break;
			}
		}
*/
		if(collide) {
			std::cerr << "collide y!\n";
			break;
		}

		//we don't adjust the position on the last time through, since it's only
		//used to see if there was a collision after the last movement, and
		//doesn't actually execute a movement.
		if(n < std::abs(effective_velocity_y/100)) {
			set_pos(x(), y() + dir);
		}
	}

	if(collide) {
		if(effective_velocity_y < 0 || !started_standing) {
			handle_event(effective_velocity_y < 0 ? "collide_head" : "collide_feet");
		}

		if(collide_info.damage || jump_on_info.damage) {
			game_logic::map_formula_callable* callable = new game_logic::map_formula_callable;
			callable->add("damage", variant(std::max(collide_info.damage, jump_on_info.damage)));
			variant v(callable);
			handle_event("collide_damage", callable);
		}
	}

	collide = false;

	for(int n = 0; n < std::abs(effective_velocity_x/100) && !collide && !type_->ignore_collide(); ++n) {
		if(type_->object_level_collisions() && non_solid_entity_collides_with_level(lvl, *this)) {
			handle_event("collide_level");
		}

		const int dir = effective_velocity_x/100 > 0 ? 1 : -1;
		const int original_y = y();
		
		set_pos(x() + dir, y());

		//if we go up or down a slope, and we began the frame standing,
		//move the character up or down as appropriate to try to keep
		//them standing.

		const bool standing = is_standing(lvl);
		if(started_standing && !standing) {
			int max_drop = 2;
			while(--max_drop && !is_standing(lvl)) {
				set_pos(x(), y()+1);

				if(entity_collides(lvl, *this, MOVE_NONE)) {
					set_pos(x(), y()-1);
					break;
				}
			}
		} else if(standing) {
			int max_slope = 5;
			while(--max_slope && is_standing(lvl)) {
				set_pos(x(), y()-1);
			}

			if(!max_slope) {
				set_pos(x(), original_y);
			} else {
				set_pos(x(), y()+1);
				if(entity_collides(lvl, *this, MOVE_NONE)) {
					set_pos(x(), original_y);
				}
			}
		}

		if(entity_collides(lvl, *this, dir > 0 ? MOVE_RIGHT : MOVE_LEFT, &collide_info)) {
			collide = true;
		}

		if(collide) {
			//undo the move to cancel out the collision
			set_pos(x() - dir, original_y);
			break;
		}
	}

	if(collide) {
		handle_event("collide");
		if(collide_info.damage) {
			game_logic::map_formula_callable* callable = new game_logic::map_formula_callable;
			callable->add("damage", variant(collide_info.damage));
			variant v(callable);
			handle_event("collide_damage", callable);
		}
	}

	stand_info = collision_info();
	is_standing(lvl, &stand_info);

	if(standing_on_ && standing_on_ != stand_info.collide_with) {
		//we were previously standing on an object and we're not anymore.
		//add the object we were standing on's velocity to ours
		velocity_x_ += standing_on_->last_move_x()*100;
		velocity_y_ += standing_on_->last_move_y()*100;
	}

	if(stand_info.collide_with && standing_on_ != stand_info.collide_with) {
		//we are standing on a new object. Adjust our velocity relative to
		//the object we're standing on
		velocity_x_ -= stand_info.collide_with->last_move_x()*100;
	}

	standing_on_ = stand_info.collide_with;
	if(standing_on_) {
		standing_on_prev_x_ = standing_on_->feet_x();
		standing_on_prev_y_ = standing_on_->feet_y();
	}

	if(!invincible_) {
		if(on_players_side()) {
			entity_ptr collide_with = lvl_->collide(body_rect(), this);
			if(collide_with && collide_with->body_harmful()) {
				handle_event("get_hit");
			}
		} else {
			entity_ptr player = lvl.hit_by_player(body_rect());
			if(player && (last_hit_by_ != player || last_hit_by_anim_ != player->current_animation_id())) {
				last_hit_by_ = player;
				last_hit_by_anim_ = player->current_animation_id();
				handle_event("hit_by_player");
			}

			if(driver_) {
				//if this is a vehicle with a driver, handle the driver being
				//hit by the player.
				entity_ptr player = lvl.hit_by_player(driver_->body_rect());
				if(player && (last_hit_by_ != player || last_hit_by_anim_ != player->current_animation_id())) {
					last_hit_by_ = player;
					last_hit_by_anim_ = player->current_animation_id();
					handle_event("driver_hit_by_player");
				}
			}
		}
	}

	if(lvl.players().empty() == false) {
		lvl.set_touched_player(lvl.players().front());
	}

	if(fall_through_platforms_ > 0) {
		--fall_through_platforms_;
	}

	static const std::string ProcessStr = "process";
	handle_event(ProcessStr);
	handle_event("process_" + frame_name_);

	if(type_->timer_frequency() > 0 && (cycle_%type_->timer_frequency()) == 0) {
		static const std::string TimerStr = "timer";
		handle_event(TimerStr);
	}
	
	const bool is_underwater = lvl.is_underwater(body_rect());
	if( is_underwater && !was_underwater_){
		//event on_enter_water
		const static std::string EnterWaterStr = "enter_water";
		handle_event(EnterWaterStr);
		was_underwater_ = true;
	}else if ( !is_underwater && was_underwater_ ){
		//event on_exit_water
		const static std::string ExitWaterStr = "exit_water";
		handle_event(ExitWaterStr);
		was_underwater_ = false;
	}

	for(std::map<std::string, particle_system_ptr>::iterator i = particle_systems_.begin(); i != particle_systems_.end(); ) {
		i->second->process(*this);
		if(i->second->is_destroyed()) {
			particle_systems_.erase(i++);
		} else {
			++i;
		}
	}

	set_driver_position();

	if(blur_) {
		blur_->next_frame(start_x, start_y, x(), y(), frame_, time_in_frame_, face_right(), upside_down(), rotate_);
		if(blur_->destroyed()) {
			blur_.reset();
		}
	}
}

void custom_object::set_driver_position()
{
	if(driver_) {
		const int pos_right = x() + type_->passenger_x();
		const int pos_left = x() + current_frame().width() - driver_->current_frame().width() - type_->passenger_x();
		driver_->set_face_right(face_right());

		driver_->set_pos(face_right() ? pos_right : pos_left, y() + type_->passenger_y());
	}
}

int custom_object::zorder() const
{
	return zorder_;
}

int custom_object::velocity_x() const
{
	return velocity_x_;
}

int custom_object::velocity_y() const
{
	return velocity_y_;
}

int custom_object::surface_friction() const
{
	return type_->surface_friction();
}

int custom_object::surface_traction() const
{
	return type_->surface_traction();
}

bool custom_object::has_feet() const
{
	return type_->has_feet() && solid();
}

bool custom_object::is_standable(int xpos, int ypos, int* friction, int* traction, int* adjust_y) const
{
	if(!body_passthrough() && springiness() == 0 && !body_harmful() && point_collides(xpos, ypos)) {
		if(friction) {
			*friction = type_->surface_friction();
		}

		if(traction) {
			*traction = type_->surface_traction();
		}

		if(adjust_y) {
			if(type_->use_image_for_collisions()) {
				for(*adjust_y = 0; point_collides(xpos, ypos - *adjust_y - 1); --(*adjust_y)) {
				}
			} else {
				*adjust_y = ypos - body_rect().y();
			}
		}

		return true;
	}

	if(frame_->has_platform()) {
		const frame& f = *frame_;
		int y1 = y() + f.platform_y();
		int y2 = previous_y_ + f.platform_y();

		if(y1 > y2) {
			std::swap(y1, y2);
		}

		if(ypos < y1 || ypos > y2) {
			return false;
		}

		if(xpos < x() + f.platform_x() || xpos >= x() + f.platform_x() + f.platform_w()) {
			return false;
		}

		if(friction) {
			*friction = type_->surface_friction();
		}

		if(traction) {
			*traction = type_->surface_traction();
		}

		if(adjust_y) {
			*adjust_y = y() + f.platform_y() - ypos;
		}

		return true;
	}

	return false;
}

void custom_object::stood_on_by(const entity_ptr& ch)
{
	handle_event("stood_on");
	stood_on_by_.push_back(ch);
}

bool custom_object::destroyed() const
{
	return hitpoints_ <= 0;
}

bool custom_object::point_collides(int xpos, int ypos) const
{
	if(type_->use_image_for_collisions()) {
		const bool result = !current_frame().is_alpha(xpos - x(), ypos - y(), time_in_frame_, face_right());
		return result;
	} else {
		return point_in_rect(point(xpos, ypos), body_rect());
	}
}

bool custom_object::rect_collides(const rect& r) const
{
	if(type_->use_image_for_collisions()) {
		rect myrect(x(), y(), current_frame().width(), current_frame().height());
		if(rects_intersect(myrect, r)) {
			rect intersection = intersection_rect(myrect, r);
			for(int y = intersection.y(); y < intersection.y2(); ++y) {
				for(int x = intersection.x(); x < intersection.x2(); ++x) {
					if(point_collides(x, y)) {
						return true;
					}
				}
			}

			return false;
		} else {
			return false;
		}
	} else {
		return rects_intersect(r, body_rect());
	}
}

const_solid_info_ptr custom_object::solid() const
{
	if(!type_->has_solid()) {
		return const_solid_info_ptr();
	}

	if(current_frame().solid()) {
		return current_frame().solid();
	}

	return type_->solid();
}

const_solid_info_ptr custom_object::platform() const
{
	return type_->platform();
}

bool custom_object::on_players_side() const
{
	return type_->on_players_side() || is_human();
}

void custom_object::control(const level& lvl)
{
}

bool custom_object::is_standing(const level& lvl, collision_info* info) const
{
	return has_feet() &&
	       point_standable(lvl, *this, feet_x(), feet_y(), info, fall_through_platforms_ ? SOLID_ONLY : SOLID_AND_PLATFORMS);
}

namespace {
typedef variant (*object_accessor)(const custom_object& obj);
//typedef boost::function<variant(const custom_object& obj)> object_accessor;
std::map<std::string, object_accessor> object_accessor_map;
}

//A utility class which is used to calculate the value of a custom object's
//attributes for the formula system.
struct custom_object::Accessor {
#define CUSTOM_ACCESSOR(name, expression) static variant name(const custom_object& obj) { return variant(expression); }
#define SIMPLE_ACCESSOR(name) static variant name(const custom_object& obj) { return variant(obj.name##_); }
	CUSTOM_ACCESSOR(type, obj.type_->id())
	CUSTOM_ACCESSOR(time_in_animation, obj.time_in_frame_)
	CUSTOM_ACCESSOR(level, obj.lvl_)
	CUSTOM_ACCESSOR(animation, obj.frame_name_)
	SIMPLE_ACCESSOR(hitpoints)
	CUSTOM_ACCESSOR(max_hitpoints, obj.type_->hitpoints())
	CUSTOM_ACCESSOR(mass, obj.type_->mass())
	CUSTOM_ACCESSOR(label, obj.label())
	CUSTOM_ACCESSOR(x, obj.x())
	CUSTOM_ACCESSOR(y, obj.y())
	CUSTOM_ACCESSOR(z, obj.zorder_)
	CUSTOM_ACCESSOR(x1, obj.body_rect().x())
	CUSTOM_ACCESSOR(y1, obj.body_rect().y())
	CUSTOM_ACCESSOR(x2, obj.body_rect().x2())
	CUSTOM_ACCESSOR(y2, obj.body_rect().y2())
	CUSTOM_ACCESSOR(w, obj.body_rect().w())
	CUSTOM_ACCESSOR(h, obj.body_rect().h())

	//note that we're taking the image midpoint, NOT the collision-rect midpoint
	//in practice, we've always calculated this from the image for our scripting,
	//and many object actually lack non-zero collision-rect widths.
	CUSTOM_ACCESSOR(midpoint_x, obj.x() + obj.current_frame().width()/2)
	CUSTOM_ACCESSOR(midpoint_y, obj.y() + obj.current_frame().height()/2)

	CUSTOM_ACCESSOR(img_w, obj.current_frame().width());
	CUSTOM_ACCESSOR(img_h, obj.current_frame().height());
	CUSTOM_ACCESSOR(front, obj.face_right() ? obj.body_rect().x2() : obj.body_rect().x());
	CUSTOM_ACCESSOR(back, obj.face_right() ? obj.body_rect().x() : obj.body_rect().x2());
	SIMPLE_ACCESSOR(cycle);
	CUSTOM_ACCESSOR(facing, obj.face_right() ? 1 : -1);
	CUSTOM_ACCESSOR(upside_down, obj.upside_down() ? 1 : 0);
	CUSTOM_ACCESSOR(up, obj.upside_down() ? 1 : -1);
	CUSTOM_ACCESSOR(down, obj.upside_down() ? -1 : 1);
	SIMPLE_ACCESSOR(velocity_x);
	SIMPLE_ACCESSOR(velocity_y);
	SIMPLE_ACCESSOR(accel_x);
	SIMPLE_ACCESSOR(accel_y);
	CUSTOM_ACCESSOR(vars, obj.vars_.get());
	CUSTOM_ACCESSOR(tmp, obj.tmp_vars_.get());
	CUSTOM_ACCESSOR(tags, obj.tags_.get());
	CUSTOM_ACCESSOR(group, obj.group());
	SIMPLE_ACCESSOR(rotate);
	CUSTOM_ACCESSOR(me, &obj);
	CUSTOM_ACCESSOR(stood_on, obj.stood_on_by_.size());
	CUSTOM_ACCESSOR(red, obj.draw_color().r());
	CUSTOM_ACCESSOR(green, obj.draw_color().g());
	CUSTOM_ACCESSOR(blue, obj.draw_color().b());
	CUSTOM_ACCESSOR(alpha, obj.draw_color().a());
	CUSTOM_ACCESSOR(damage, obj.current_frame().damage());
	CUSTOM_ACCESSOR(hit_by, obj.last_hit_by_.get());
	CUSTOM_ACCESSOR(jumped_on_by, obj.last_jumped_on_by_.get());
	CUSTOM_ACCESSOR(distortion, obj.distortion_.get());
	CUSTOM_ACCESSOR(is_standing, (obj.lvl_ ? variant(obj.is_standing(*obj.lvl_)) : variant()));
	CUSTOM_ACCESSOR(near_cliff_edge, obj.is_standing(*obj.lvl_) && cliff_edge_within(*obj.lvl_, obj.feet_x(), obj.feet_y(), obj.face_dir()*15));
	CUSTOM_ACCESSOR(distance_to_cliff, ::distance_to_cliff(*obj.lvl_, obj.feet_x(), obj.feet_y(), obj.face_dir()));
	CUSTOM_ACCESSOR(slope_standing_on, -obj.slope_standing_on(obj.type_->feet_width()*2)*obj.face_dir());
	CUSTOM_ACCESSOR(underwater, obj.lvl_->is_underwater(obj.body_rect()));
	CUSTOM_ACCESSOR(driver, obj.driver_ ? obj.driver_.get() : &obj);
	CUSTOM_ACCESSOR(is_human, obj.is_human() ? 1 : 0);
	SIMPLE_ACCESSOR(invincible);
	CUSTOM_ACCESSOR(springiness, obj.springiness());
	CUSTOM_ACCESSOR(destroyed, obj.destroyed());
#undef SIMPLE_ACCESSOR
#undef CUSTOM_ACCESSOR

	static variant is_standing_on_platform(const custom_object& obj) {
		if(!obj.lvl_) {
			return variant();
		}

		collision_info info;
		obj.is_standing(*obj.lvl_, &info);
		return variant(info.platform);
	}

	static variant standing_on(const custom_object& obj) {
		if(!obj.lvl_) {
			return variant();
		}
		
		entity_ptr stand_on;
		collision_info info;
		obj.is_standing(*obj.lvl_, &info);
		return variant(info.collide_with.get());
	}

#define CUSTOM_ACCESSOR(name, expression) static variant name(const custom_object& obj) { return variant(expression); }

	static void init() {
#define ACCESSOR(name) object_accessor_map.insert(std::pair<std::string,object_accessor>(#name, name))
		ACCESSOR(type);
		ACCESSOR(time_in_animation);
		ACCESSOR(level);
		ACCESSOR(animation);
		ACCESSOR(hitpoints);
		ACCESSOR(max_hitpoints);
		ACCESSOR(mass);
		ACCESSOR(label);
		ACCESSOR(x);
		ACCESSOR(y);
		ACCESSOR(z);
		ACCESSOR(x1);
		ACCESSOR(y1);
		ACCESSOR(x2);
		ACCESSOR(y2);
		ACCESSOR(w);
		ACCESSOR(h);
		ACCESSOR(midpoint_x);
		ACCESSOR(midpoint_y);
		ACCESSOR(img_w);
		ACCESSOR(img_h);
		ACCESSOR(front);
		ACCESSOR(back);
		ACCESSOR(cycle);
		ACCESSOR(facing);
		ACCESSOR(upside_down);
		ACCESSOR(up);
		ACCESSOR(down);
		ACCESSOR(velocity_x);
		ACCESSOR(velocity_y);
		ACCESSOR(accel_x);
		ACCESSOR(accel_y);
		ACCESSOR(vars);
		ACCESSOR(tmp);
		ACCESSOR(tags);
		ACCESSOR(group);
		ACCESSOR(rotate);
		ACCESSOR(me);
		ACCESSOR(stood_on);
		ACCESSOR(red);
		ACCESSOR(green);
		ACCESSOR(blue);
		ACCESSOR(alpha);
		ACCESSOR(damage);
		ACCESSOR(hit_by);
		ACCESSOR(jumped_on_by);
		ACCESSOR(distortion);
		ACCESSOR(is_standing);
		ACCESSOR(is_standing_on_platform);
		ACCESSOR(near_cliff_edge);
		ACCESSOR(distance_to_cliff);
		ACCESSOR(slope_standing_on);
		ACCESSOR(underwater);
		ACCESSOR(driver);
		ACCESSOR(is_human);
		ACCESSOR(invincible);
		ACCESSOR(springiness);
		ACCESSOR(destroyed);
		ACCESSOR(standing_on);
	}
};

void custom_object::init()
{
	Accessor::init();
}

variant custom_object::get_value(const std::string& key) const
{
	std::map<std::string, object_accessor>::const_iterator accessor_itor = object_accessor_map.find(key);
	if(accessor_itor != object_accessor_map.end()) {
		return accessor_itor->second(*this);
	}

	variant var_result = tmp_vars_->query_value(key);
	if(!var_result.is_null()) {
		return var_result;
	}

	var_result = vars_->query_value(key);
	if(!var_result.is_null()) {
		return var_result;
	}

	std::map<std::string, variant>::const_iterator i = type_->variables().find(key);
	if(i != type_->variables().end()) {
		return i->second;
	}

	std::map<std::string, particle_system_ptr>::const_iterator particle_itor = particle_systems_.find(key);
	if(particle_itor != particle_systems_.end()) {
		return variant(particle_itor->second.get());
	}

	return variant();
}

void custom_object::get_inputs(std::vector<game_logic::formula_input>* inputs) const
{
	inputs->push_back(game_logic::formula_input("time_in_animation", game_logic::FORMULA_READ_WRITE));
	inputs->push_back(game_logic::formula_input("level", game_logic::FORMULA_READ_ONLY));
	inputs->push_back(game_logic::formula_input("animation", game_logic::FORMULA_READ_ONLY));
	inputs->push_back(game_logic::formula_input("hitpoints", game_logic::FORMULA_READ_WRITE));
}

void custom_object::set_value(const std::string& key, const variant& value)
{
	if(key == "animation") {
		set_frame(value.as_string());
	} else if(key == "time_in_animation") {
		time_in_frame_ = value.as_int();
	} else if(key == "x") {
		set_x(value.as_int());
	} else if(key == "y") {
		set_y(value.as_int());
	} else if(key == "z") {
		zorder_ = value.as_int();
	} else if(key == "midpoint_x") {
		set_pos(value.as_int() - body_rect().w()/2, y());
	} else if(key == "midpoint_y") {
		set_pos(x(), value.as_int() - body_rect().h()/2);
	} else if(key == "facing") {
		set_face_right(value.as_int() > 0);
	} else if(key == "upside_down") {
		set_upside_down(value.as_int());
	} else if(key == "hitpoints") {
		hitpoints_ = value.as_int();
		if(hitpoints_ <= 0) {
			die();
		}
	} else if(key == "velocity_x") {
		velocity_x_ = value.as_int();
	} else if(key == "velocity_y") {
		velocity_y_ = value.as_int();
	} else if(key == "accel_x") {
		accel_x_ = value.as_int();
	} else if(key == "accel_y") {
		accel_y_ = value.as_int();
	} else if(key == "rotate") {
		rotate_ = value.as_int();
	} else if(key == "red") {
		make_draw_color();
		draw_color_->buf()[0] = truncate_to_char(value.as_int());
	} else if(key == "green") {
		make_draw_color();
		draw_color_->buf()[1] = truncate_to_char(value.as_int());
	} else if(key == "blue") {
		make_draw_color();
		draw_color_->buf()[2] = truncate_to_char(value.as_int());
	} else if(key == "alpha") {
		make_draw_color();
		draw_color_->buf()[3] = truncate_to_char(value.as_int());
	} else if(key == "brightness"){
		make_draw_color();
		draw_color_->buf()[0] = value.as_int();
		draw_color_->buf()[1] = value.as_int();
		draw_color_->buf()[2] = value.as_int();
	} else if(key == "distortion") {
		distortion_ = value.try_convert<graphics::raster_distortion>();
	} else if(key == "current_generator") {
		set_current_generator(value.try_convert<current_generator>());
	} else if(key == "invincible") {
		invincible_ = value.as_int();
	} else if(key == "fall_through_platforms") {
		fall_through_platforms_ = value.as_int();
	} else if(key == "tags") {
		if(value.is_list()) {
			tags_ = new game_logic::map_formula_callable;
			for(int n = 0; n != value.num_elements(); ++n) {
				tags_->add(value[n].as_string(), variant(1));
			}
		}
	} else {
		vars_->add(key, value);
	}
}

void custom_object::set_frame(const std::string& name)
{
	const std::string previous_animation = frame_name_;

	//fire an event to say that we're leaving the current frame.
	if(frame_ && name != frame_name_) {
		handle_event("leave_" + frame_name_ + "_anim");
	}

	const int start_x = feet_x();
	const int start_y = feet_y();

	frame_ = &type_->get_frame(name);
	++current_animation_id_;

	const int diff_x = feet_x() - start_x;
	const int diff_y = feet_y() - start_y;

	set_pos(x() - diff_x, y() - diff_y);

	//TODO: check if we are now colliding with something, and handle properly.

	frame_name_ = name;
	time_in_frame_ = 0;
	if(frame_->velocity_x() != INT_MIN) {
		velocity_x_ = frame_->velocity_x() * (face_right() ? 1 : -1);
	}

	if(frame_->velocity_y() != INT_MIN) {
		velocity_y_ = frame_->velocity_y();
	}

	if(frame_->accel_x() != INT_MIN) {
		accel_x_ = frame_->accel_x();
	}
	
	if(frame_->accel_y() != INT_MIN) {
		accel_y_ = frame_->accel_y();
	}
	
	frame_->play_sound(this);

	if(lvl_ && entity_collides_with_level(*lvl_, *this, MOVE_NONE)) {
		game_logic::map_formula_callable* callable(new game_logic::map_formula_callable(this));
		callable->add("previous_animation", variant(previous_animation));
		game_logic::formula_callable_ptr callable_ptr(callable);
		handle_event("change_animation_failure", callable);
		handle_event("change_animation_failure_" + frame_name_, callable);
		assert(!entity_collides_with_level(*lvl_, *this, MOVE_NONE));
	}

	handle_event("enter_anim");
	handle_event("enter_" + frame_name_ + "_anim");
}

void custom_object::die()
{
	hitpoints_ = 0;
	handle_event("die");
}

void custom_object::hit_player()
{
	handle_event("hit_player");
}

void custom_object::hit_by(entity& e)
{
	std::cerr << "hit_by!\n";
	last_hit_by_ = &e;
	handle_event("hit_by_player");
}

void custom_object::move_to_standing(level& lvl)
{
	int start_y = y();
	lvl_ = &lvl;
	//descend from the initial-position (what the player was at in the prev level) until we're standing
	for(int n = 0; n != 10000; ++n) {
		if(is_standing(lvl)) {
			
			if(n == 0) {  //if we've somehow managed to be standing on the very first frame, try to avoid the possibility that this is actually some open space underground on a cave level by scanning up till we reach the surface.
				for(int n = 0; n != 10000; ++n) {
					set_pos(x(), y() - 1);
					if(!is_standing(lvl)) {
						set_pos(x(), y() + 1);
						
						if(y() < lvl.boundaries().y()) {
							//we are too high, out of the level. Move the
							//character down, under the solid, and then
							//call this function again to move them down
							//to standing on the solid below.
							for(int n = 0; n != 10000; ++n) {
								set_pos(x(), y() + 1);
								if(!is_standing(lvl)) {
									move_to_standing(lvl);
									return;
								}
							}
						}
						
						return;
					}
				}
				return;
			}
			return;
		}
		
		set_pos(x(), y() + 1);
	}
	
	set_pos(x(), start_y);
	std::cerr << "MOVE_TO_STANDING FAILED\n";
}


bool custom_object::dies_on_inactive() const
{
	return type_->dies_on_inactive();
}

bool custom_object::always_active() const
{
	return type_->always_active();
}

bool custom_object::body_harmful() const
{
	return type_->body_harmful();
}

bool custom_object::body_passthrough() const
{
	return type_->body_passthrough();
}

int custom_object::springiness() const
{
	return type_->springiness();
}

bool custom_object::spring_off_head(entity& landed_on_by)
{
	last_jumped_on_by_ = entity_ptr(&landed_on_by);
	handle_event("jumped_on");
	return true;
}

const frame& custom_object::portrait_frame() const
{
	return type_->get_frame("portrait");
}

const frame& custom_object::icon_frame() const
{
	return type_->default_frame();
}

entity_ptr custom_object::clone() const
{
	entity_ptr res(new custom_object(*this));
	res->set_distinct_label();
	return res;
}

entity_ptr custom_object::backup() const
{
	entity_ptr res(new custom_object(*this));
	return res;
}

void custom_object::handle_event(const std::string& event, const formula_callable* context)
{
	if(hitpoints_ <= 0 && event != "die") {
		return;
	}

	std::vector<game_logic::const_formula_ptr> handlers;

	std::map<std::string, game_logic::const_formula_ptr>::const_iterator handler_itor = event_handlers_.find(event);
	if(handler_itor != event_handlers_.end()) {
		game_logic::const_formula_ptr handler = handler_itor->second;
		if(handler) {
			handlers.push_back(handler);
		}
	}


	game_logic::const_formula_ptr handler = type_->get_event_handler(event);
	if(handler) {
		handlers.push_back(handler);
	}

	foreach(const game_logic::const_formula_ptr& handler, handlers) {
		variant var;

		if(context) {
			game_logic::formula_callable_with_backup callable(*this, *context);
			var = handler->execute(callable);
		} else {
			var = handler->execute(*this);
		}

		const bool result = execute_command(var);
		if(!result) {
			break;
		}
	}
}

bool custom_object::execute_command(const variant& var)
{
	bool result = true;
	if(var.is_null()) { return result; }
	if(var.is_list()) {
		for(int n = 0; n != var.num_elements(); ++n) {
			result = execute_command(var[n]) && result;
		}
	} else {
		custom_object_command_callable* cmd = var.try_convert<custom_object_command_callable>();
		if(cmd != NULL) {
			cmd->execute(*lvl_, *this);
		} else {
			entity_command_callable* cmd = var.try_convert<entity_command_callable>();
			if(cmd != NULL) {
				cmd->execute(*lvl_, *this);
			} else {
				if(var.try_convert<swallow_object_command_callable>()) {
					result = false;
				}
			}
		}
	}

	return result;
}

int custom_object::slope_standing_on(int range) const
{
	if(lvl_ == NULL || !is_standing(*lvl_)) {
		return 0;
	}

	const int forward = face_right() ? 1 : -1;
	const int xpos = feet_x();
	int ypos = feet_y();


	for(int n = 0; !lvl_->solid(xpos, ypos) && n != 10; ++n) {
		++ypos;
	}

	if(range == 1) {
		if(lvl_->solid(xpos + forward, ypos - 1) &&
		   !lvl_->solid(xpos - forward, ypos)) {
			return 45;
		}

		if(!lvl_->solid(xpos + forward, ypos) &&
		   lvl_->solid(xpos - forward, ypos - 1)) {
			return -45;
		}

		return 0;
	} else {
		if(!is_standing(*lvl_)) {
			return 0;
		}

		int y1 = find_ground_level(*lvl_, xpos + forward*range, ypos, range+1);
		int y2 = find_ground_level(*lvl_, xpos - forward*range, ypos, range+1);
		while((y1 == INT_MIN || y2 == INT_MIN) && range > 0) {
			y1 = find_ground_level(*lvl_, xpos + forward*range, ypos, range+1);
			y2 = find_ground_level(*lvl_, xpos - forward*range, ypos, range+1);
			--range;
		}

		if(range == 0) {
			return 0;
		}

		const int dy = y2 - y1;
		const int dx = range*2;
		return (dy*45)/dx;
	}
}

void custom_object::make_draw_color()
{
	if(!draw_color_.get()) {
		draw_color_.reset(new graphics::color_transform(draw_color()));
	}
}

const graphics::color_transform& custom_object::draw_color() const
{
	if(draw_color_.get()) {
		return *draw_color_;
	}

	static const graphics::color_transform white(0xFF, 0xFF, 0xFF, 0xFF);
	return white;
}

game_logic::const_formula_ptr custom_object::get_event_handler(const std::string& key) const
{
	std::map<std::string, game_logic::const_formula_ptr>::const_iterator itor = event_handlers_.find(key);
	if(itor != event_handlers_.end()) {
		return itor->second;
	}

	return game_logic::const_formula_ptr();
}

void custom_object::set_event_handler(const std::string& key, game_logic::const_formula_ptr f)
{
	if(!f) {
		event_handlers_.erase(key);
	} else {
		event_handlers_[key] = f;
	}
}

bool custom_object::can_interact_with() const
{
	return can_interact_with_;
}

std::string custom_object::debug_description() const
{
	return type_->id();
}

void custom_object::map_entities(const std::map<entity_ptr, entity_ptr>& m)
{
	if(last_hit_by_) {
		std::map<entity_ptr, entity_ptr>::const_iterator i = m.find(last_hit_by_);
		if(i != m.end()) {
			last_hit_by_ = i->second;
		}
	}

	foreach(entity_ptr& e, stood_on_by_) {
		std::map<entity_ptr, entity_ptr>::const_iterator i = m.find(e);
		if(i != m.end()) {
			e = i->second;
		}
	}
}

void custom_object::add_particle_system(const std::string& key, const std::string& type)
{
	particle_systems_[key] = type_->get_particle_system_factory(type)->create(*this);
}

void custom_object::remove_particle_system(const std::string& key)
{
	particle_systems_.erase(key);
}

void custom_object::set_text(const std::string& text, const std::string& font)
{
	text_.reset(new custom_object_text);
	text_->text = text;
	text_->font = graphical_font::get(font);
}

bool custom_object::boardable_vehicle() const
{
	return type_->is_vehicle() && driver_.get() == NULL;
}

void custom_object::boarded(level& lvl, const entity_ptr& player)
{
	if(!player) {
		return;
	}

	player->board_vehicle();

	if(player->is_human()) {
		playable_custom_object* new_player(new playable_custom_object(*this));
		new_player->driver_ = player;

		lvl.add_player(new_player);

		new_player->get_player_info()->swap_player_state(*player->get_player_info());
		lvl.remove_character(this);
	} else {
		driver_ = player;
		lvl.remove_character(player);
	}
}

void custom_object::unboarded(level& lvl)
{
	if(velocity_x() > 100) {
		driver_->set_face_right(false);
	}

	if(velocity_x() < -100) {
		driver_->set_face_right(true);
	}

	if(is_human()) {
		custom_object* vehicle(new custom_object(*this));
		vehicle->driver_ = entity_ptr();
		lvl.add_character(vehicle);

		lvl.add_player(driver_);

		driver_->unboard_vehicle();

		driver_->get_player_info()->swap_player_state(*get_player_info());
	} else {
		lvl.add_character(driver_);
		driver_->unboard_vehicle();
		driver_ = entity_ptr();
	}
}

void custom_object::board_vehicle()
{
}

void custom_object::unboard_vehicle()
{
}

void custom_object::set_blur(const blur_info* blur)
{
	if(blur) {
		if(blur_) {
			blur_->copy_settings(*blur); 
		} else {
			blur_.reset(new blur_info(*blur));
		}
	} else {
		blur_.reset();
	}
}

BENCHMARK_ARG(custom_object_get_attr, const std::string& attr)
{
	static custom_object* obj = new custom_object("black_ant", 0, 0, false);
	BENCHMARK_LOOP {
		obj->query_value(attr);
	}
}

BENCHMARK_ARG_CALL(custom_object_get_attr, easy_lookup, "x");
BENCHMARK_ARG_CALL(custom_object_get_attr, hard_lookup, "xxxx");

BENCHMARK_ARG(custom_object_handle_event, const std::string& object_event)
{
	std::string::const_iterator i = std::find(object_event.begin(), object_event.end(), ':');
	ASSERT_LOG(i != object_event.end(), "custom_object_event_handle argument must have a pipe seperator: " << object_event);
	std::string obj_type(object_event.begin(), i);
	std::string event_name(i+1, object_event.end());
	static level* lvl = new level("titlescreen.cfg");
	static custom_object* obj = new custom_object(obj_type, 0, 0, false);
	obj->set_level(*lvl);
	BENCHMARK_LOOP {
		obj->handle_event(event_name);
	}
}

BENCHMARK_ARG_CALL(custom_object_handle_event, ant_collide, "black_ant:collide");
BENCHMARK_ARG_CALL(custom_object_handle_event, ant_non_exist, "black_ant:blahblah");

BENCHMARK_ARG_CALL_COMMAND_LINE(custom_object_handle_event);
