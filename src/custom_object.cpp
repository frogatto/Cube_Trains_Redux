#include <cassert>
#include <iostream>

#include "custom_object.hpp"
#include "custom_object_functions.hpp"
#include "font.hpp"
#include "formatter.hpp"
#include "level.hpp"
#include "level_logic.hpp"
#include "raster.hpp"
#include "wml_node.hpp"
#include "wml_utils.hpp"
#include "utils.hpp"


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
	velocity_x_(0), velocity_y_(0),
	rotate_(0), zorder_(wml::get_int(node, "zorder", type_->zorder())),
	hitpoints_(wml::get_int(node, "hitpoints", type_->hitpoints())),
	was_underwater_(false),
	lvl_(NULL),
	vars_(new game_logic::map_formula_callable(node->get_child("vars"))),
	last_hit_by_anim_(0),
	cycle_(wml::get_int(node, "cycle"))
{
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
	std::cerr << "custom_object: " << body_rect().w() << "\n";

	wml::const_node_ptr editor_info = node->get_child("editor_info");
	if(editor_info) {
		std::cerr << "CREATE EDITOR INFO\n";
		set_editor_info(const_editor_entity_info_ptr(new editor_entity_info(editor_info)));
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
	rotate_(0), zorder_(type_->zorder()),
	hitpoints_(type_->hitpoints()),
	was_underwater_(false),
	lvl_(NULL),
	vars_(new game_logic::map_formula_callable),
	last_hit_by_anim_(0),
	cycle_(0)
{
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
	std::cerr << "DESTROY CUSTOM OBJECT\n";
}

wml::node_ptr custom_object::write() const
{
	wml::node_ptr res(new wml::node("character"));
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

	if(custom_type_) {
		res->add_child(wml::deep_copy(custom_type_));
	}

	if(editor_info()) {
		res->add_child(editor_info()->write());
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

	if(draw_color_) {
		draw_color_->to_color().set_as_current_color();
	}

	frame_->draw(x(), y(), face_right(), upside_down(), time_in_frame_, rotate_);

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
	const bool started_standing = is_standing(lvl);
	previous_y_ = y();
	const int start_x = x();
	++cycle_;
	lvl_ = &lvl;

	if(cycle_ == 1) {
		handle_event("create");
		handle_event("done_create");
	}

	lvl_ = &lvl;

	++time_in_frame_;

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

	velocity_x_ += frame_->accel_x() * (face_right() ? 1 : -1);
	velocity_y_ += frame_->accel_y();

	if(type_->friction()) {
		velocity_x_ = (velocity_x_*(100-type_->friction()))/100;
		velocity_y_ = (velocity_y_*(100-type_->friction()))/100;
	}

	if(type_->affected_by_currents()) {
		lvl.get_current(*this, &velocity_x_, &velocity_y_);
	}

	bool collide = false;

	if(type_->ignore_collide()) {
		move_centipixels(velocity_x_, velocity_y_);
	}

	for(int n = 0; n <= std::abs(velocity_x_/100) && !collide && !type_->ignore_collide(); ++n) {
		const int dir = velocity_x_/100 > 0 ? 1 : -1;
		int xpos = dir < 0 ? body_rect().x() : (body_rect().x2() - 1);

		const int ybegin = y() + current_frame().collide_y();
		const int yend = ybegin + current_frame().collide_h();
		int damage = 0;
		for(int ypos = ybegin; ypos != yend; ++ypos) {
			if(lvl.solid(xpos, ypos, NULL, &damage)) {
				collide = true;
				break;
			}
		}

		if(!collide && !body_passthrough()) {
			entity_ptr collide_with = lvl.collide(rect(xpos, ybegin, 1, yend - ybegin), this);
			if(collide_with.get() != NULL) {
				game_logic::formula_callable_ptr callable(new collide_with_callable(collide_with.get()));
				std::cerr << "collide_with\n";
				handle_event("collide_with", callable.get());
				collide = true;
			}
		}

		if(collide) {
			//undo the move to cancel out the collision
			if(n != 0) {
				set_pos(x() - dir, y());
			}
			break;
		}

		//we don't adjust the position on the last time through, since it's only
		//used to see if there was a collision after the last movement, and
		//doesn't actually execute a movement.
		if(n < std::abs(velocity_x_/100)) {
			set_pos(x() + dir, y());
		}
	}

	if(collide) {
		handle_event("collide");
	}

	if(!body_passthrough()) {
		entity_ptr collide_with = lvl.collide(body_rect(), this);
		if(collide_with.get() != NULL) {
				game_logic::formula_callable_ptr callable(new collide_with_callable(collide_with.get()));
			std::cerr << "collide_with\n";
			handle_event("collide_with", callable.get());
			collide = true;
		}
	}

	//std::cerr << "velocity_y: " << velocity_y_ << "\n";
	collide = false;
	for(int n = 0; n <= std::abs(velocity_y_/100) && !collide && !type_->ignore_collide(); ++n) {
		const int dir = velocity_y_/100 > 0 ? 1 : -1;
		int ypos = dir < 0 ? body_rect().y() : (body_rect().y2() - 1);

		const int xbegin = body_rect().x();
		const int xend = body_rect().x2();
		int damage = 0;
// TODO: at the moment we don't consider it a collision if we 
//		for(int xpos = xbegin; xpos != xend; ++xpos) {
//			if(lvl.solid(xpos, ypos, NULL, &damage)) {
//				collide = true;
//				break;
//			}
//		}

		if(!collide && velocity_y_ > 0 && is_standing(lvl)) {
			collide = true;
		}

		if(collide) {
			std::cerr << "collide y!\n";
			break;
		}

		//we don't adjust the position on the last time through, since it's only
		//used to see if there was a collision after the last movement, and
		//doesn't actually execute a movement.
		if(n < std::abs(velocity_y_/100)) {
			set_pos(x(), y() + dir);
		}
	}

	if(collide) {
		if(velocity_y_ < 0 || !started_standing) {
			handle_event(velocity_y_ < 0 ? "collide_head" : "collide_feet");
		}
	}

	if(!type_->on_players_side()) {
		character_ptr player = lvl.hit_by_player(body_rect());
		if(player && (last_hit_by_ != player || last_hit_by_anim_ != player->current_animation_id())) {
			last_hit_by_ = player;
			last_hit_by_anim_ = player->current_animation_id();
			handle_event("hit_by_player");
		}
	}

	if(lvl.player() && rects_intersect(body_rect(), lvl.player()->body_rect())) {
		handle_event(lvl.player()->enter() ? "interact" : "touch");
	}

	static const std::string ProcessStr = "process";
	handle_event(ProcessStr);

	if(type_->timer_frequency() > 0 && (cycle_%type_->timer_frequency()) == 0) {
		static const std::string TimerStr = "timer";
		handle_event(TimerStr);
	}

	//adjust anyone who is standing on us by the amount we've moved.
	if(stood_on_by_.empty() == false) {
		const int dx = x() - start_x;
		const int dy = y() - previous_y_;

		foreach(const entity_ptr& c, stood_on_by_) {
			const int start_x = c->x();
			const int start_y = c->y();
			c->set_pos(c->x() + dx, c->y() + dy);
			int damage = 0;
			if(lvl_->solid(c->body_rect(), NULL, NULL, &damage) && damage == 0) {
				//doing this movement would make the character collide with
				//something solid, so undo it.
				c->set_pos(start_x, start_y);
			}
		}
		stood_on_by_.clear();
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
			*adjust_y = ypos - body_rect().y();
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
	return point_in_rect(point(xpos, ypos), body_rect());
}

bool custom_object::on_players_side() const
{
	return type_->on_players_side();
}

void custom_object::control(const level& lvl)
{
}

bool custom_object::is_standing(const level& lvl) const
{
	return (current_frame().feet_x() || current_frame().feet_y()) &&
	       lvl.standable(feet_x(), feet_y());
}

variant custom_object::get_value(const std::string& key) const
{
	if(key == "time_in_animation") {
		return variant(time_in_frame_);
	} else if(key == "level") {
		return variant(lvl_);
	} else if(key == "animation") {
		return variant(frame_name_);
	} else if(key == "hitpoints") {
		return variant(hitpoints_);
	} else if(key == "max_hitpoints") {
		return variant(type_->hitpoints());
	} else if(key == "mass") {
		return variant(type_->mass());
	} else if(key == "label") {
		return variant(label());
	} else if(key == "x") {
		return variant(x());
	} else if(key == "y") {
		return variant(y());
	} else if(key == "z") {
		return variant(zorder_);
	} else if(key == "x1") {
		return variant(body_rect().x());
	} else if(key == "y1") {
		return variant(body_rect().y());
	} else if(key == "x2") {
		return variant(body_rect().x2());
	} else if(key == "y2") {
		return variant(body_rect().y2());
	} else if(key == "w") {
		return variant(body_rect().w());
	} else if(key == "h") {
		return variant(body_rect().h());
	} else if(key == "midpoint_x") {
		//note that we're taking the image midpoint, NOT the collision-rect midpoint
		//in practice, we've always calculated this from the image for our scripting,
		//and many object actually lack non-zero collision-rect widths.
		return variant(x() + current_frame().width()/2);
	} else if(key == "midpoint_y") {
		return variant(y() + current_frame().height()/2);
	} else if(key == "img_w") {
		return variant(current_frame().width());
	} else if(key == "img_h") {
		return variant(current_frame().height());
	} else if(key == "front") {
		return variant(face_right() ? body_rect().x2() : body_rect().x());
	} else if(key == "back") {
		return variant(face_right() ? body_rect().x() : body_rect().x2());
	} else if(key == "cycle") {
		return variant(cycle_);
	} else if(key == "facing") {
		return variant(face_right() ? 1 : -1);
	} else if(key == "upside_down") {
		return variant(upside_down() ? 1 : 0);
	} else if(key == "up") {
		return variant(upside_down() ? 1 : -1);
	} else if(key == "down") {
		return variant(upside_down() ? -1 : 1);
	} else if(key == "velocity_x") {
		return variant(velocity_x_);
	} else if(key == "velocity_y") {
		return variant(velocity_y_);
	} else if(key == "vars") {
		return variant(vars_.get());
	} else if(key == "group") {
		return variant(group());
	} else if(key == "rotate") {
		return variant(rotate_);
	} else if(key == "me") {
		return variant(this);
	} else if(key == "stood_on") {
		return variant(stood_on_by_.size());
	} else if(key == "red") {
		return variant(draw_color().r());
	} else if(key == "green") {
		return variant(draw_color().g());
	} else if(key == "blue") {
		return variant(draw_color().b());
	} else if(key == "alpha") {
		return variant(draw_color().a());
	} else if(key == "damage") {
		return variant(current_frame().damage());
	} else if(key == "hit_by") {
		return variant(last_hit_by_.get());
	} else if(key == "distortion") {
		return variant(distortion_.get());
	} else if(key == "is_standing") {
		if(!lvl_) {
			return variant();
		}
		return variant(is_standing(*lvl_));
	} else if(key == "near_cliff_edge") {
		return variant(is_standing(*lvl_) &&
		               cliff_edge_within(*lvl_, feet_x(), feet_y(), face_dir()*15));
	} else if(key == "underwater") {
		return variant(lvl_->is_underwater(body_rect()));
	}

	return vars_->query_value(key);
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
	if(key == "time_in_animation") {
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
	} else {
		vars_->add(key, value);
	}
}

void custom_object::set_frame(const std::string& name)
{
	//fire an event to say that we're leaving the current frame.
	if(frame_) {
		handle_event("leave_" + frame_name_ + "_anim");
	}

	const int start_x = feet_x();
	const int start_y = feet_y();

	frame_ = &type_->get_frame(name);

	const int diff_x = feet_x() - start_x;
	const int diff_y = feet_y() - start_y;

	set_pos(x() - diff_x, y() - diff_y);

	//try to resolve collision by adjusting position
	if(lvl_ && !type_->ignore_collide()) {
		const int MaxAdjust = 10;
		rect body = body_rect();
		const int middle = (body.y() + body.y2())/2;
		int n = 0;
		while(n != MaxAdjust && lvl_->solid(body.x() + n, middle)) {
			++n;
		}

		set_pos(x() + n, y());

		n = 0;
		while(n != MaxAdjust && lvl_->solid(body.x2() - n, middle)) {
			++n;
		}

		set_pos(x() - n, y());
	}

	frame_name_ = name;
	time_in_frame_ = 0;
	if(frame_->velocity_x()) {
		velocity_x_ = frame_->velocity_x() * (face_right() ? 1 : -1);
		std::cerr << "set_frame: " << velocity_x_ << "\n";
	}

	if(frame_->velocity_y()) {
		velocity_y_ = frame_->velocity_y();
	}

	frame_->play_sound(this);
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

bool custom_object::spring_off_head(const entity& landed_on_by)
{
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
	if ( (hitpoints_ > 0) || (event == "die"))
	{
		game_logic::const_formula_ptr handler;
		std::map<std::string, game_logic::const_formula_ptr>::const_iterator handler_itor = event_handlers_.find(event);
		if(handler_itor != event_handlers_.end()) {
			handler = handler_itor->second;
		} else {
			handler = type_->get_event_handler(event);
		}

		if(handler) {
			variant var;

			if(context) {
				game_logic::formula_callable_with_backup callable(*this, *context);
				var = handler->execute(callable);
			} else {
				var = handler->execute(*this);
			}

			execute_command(var);
		}
	}
}

void custom_object::execute_command(const variant& var)
{
	if(var.is_null()) { return; }
	if(var.is_list()) {
		for(int n = 0; n != var.num_elements(); ++n) {
			execute_command(var[n]);
		}
	} else {
		custom_object_command_callable* cmd = var.try_convert<custom_object_command_callable>();
		if(cmd != NULL) {
			std::cerr << "executing!\n";
			cmd->execute(*lvl_, *this);
		} else {
			entity_command_callable* cmd = var.try_convert<entity_command_callable>();
			if(cmd != NULL) {
				cmd->execute(*lvl_, *this);
			}
		}
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

