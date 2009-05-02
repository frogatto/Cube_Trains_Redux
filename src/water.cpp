#include <iostream>

#include "foreach.hpp"
#include "formatter.hpp"
#include "formula.hpp"
#include "level.hpp"
#include "raster.hpp"
#include "water.hpp"
#include "wml_node.hpp"
#include "wml_utils.hpp"
#include "color_utils.hpp"

water::water(wml::const_node_ptr water_node) :
  current_x_formula_(game_logic::formula::create_optional_formula(water_node->attr("current_x_formula"))),
  current_y_formula_(game_logic::formula::create_optional_formula(water_node->attr("current_y_formula")))
{
	FOREACH_WML_CHILD(area_node, water_node, "area") {
		const rect r(area_node->attr("rect"));
		areas_.push_back(area(r));
	}

	FOREACH_WML_CHILD(layer_node, water_node, "layer") {
		zorder_pos pos;
		pos.zorder = wml::get_int(layer_node, "zorder");
		pos.offset = wml::get_int(layer_node, "offset");
		pos.color = string_to_color(layer_node->attr("color"));
		
		positions_.push_back(pos);
	}
}

wml::node_ptr water::write() const
{
	wml::node_ptr result(new wml::node("water"));
	foreach(const area& a, areas_) {
		wml::node_ptr node(new wml::node("area"));
		node->set_attr("rect", a.rect_.to_string());
	}

	foreach(const zorder_pos& pos, positions_) {
		wml::node_ptr node(new wml::node("layer"));
		char buf[128];
		sprintf(buf, "%02x%02x%02x", pos.color.r, pos.color.g, pos.color.b);
		node->set_attr("color", buf);
		node->set_attr("zorder", formatter() << pos.zorder);
		node->set_attr("offset", formatter() << pos.offset);
		
		result->add_child(node);
	}
	return result;
}

void water::begin_drawing()
{
	foreach(area& a, areas_) {
		graphics::add_raster_distortion(&a.distortion_);
	}
}

void water::end_drawing()
{
	foreach(const area& a, areas_) {
		graphics::remove_raster_distortion(&a.distortion_);
	}
}

void water::set_surface_detection_rects(int zorder)
{
	const int offset = get_offset(zorder);
	foreach(area& a, areas_) {
		//detect drawing at the surface of the water.
		a.draw_detection_buf_.resize(a.rect_.w());
		memset(&a.draw_detection_buf_[0], 0, a.draw_detection_buf_.size());
		graphics::set_draw_detection_rect(rect(a.rect_.x(), a.rect_.y() + offset, a.rect_.w(), 1), &a.draw_detection_buf_[0]);
	}
}

bool water::draw(int begin_layer, int end_layer, int x, int y, int w, int h) const
{
	bool result = false;
	foreach(const area& a, areas_) {
		if(draw_area(a, begin_layer, end_layer, x, y, w, h)) {
			result = true;
		}
	}

	return result;
}

bool water::draw_area(const water::area& a, int begin_layer, int end_layer, int x, int y, int w, int h) const
{
	if(begin_layer < min_zorder()) {
		begin_layer = min_zorder();
	}
	
	if(end_layer > max_zorder()) {
		end_layer = max_zorder();
	}
	
	if(begin_layer > end_layer) {
		return false;
	}

	const SDL_Color waterline_color = {250, 240, 205, 255};

	const int offset1 = get_offset(begin_layer);
	const int offset2 = get_offset(end_layer);
	if(offset2 <= offset1) {
		return false;
	}
	
	const SDL_Rect r = {a.rect_.x(), a.rect_.y() + offset1, a.rect_.w(), offset2 - offset1};
	const SDL_Color water_color = get_color(offset1);
	graphics::draw_rect(r, water_color, 200);

	if(begin_layer == min_zorder()) {
		const SDL_Rect water_surface_rect = {a.rect_.x(), a.rect_.y() + offset1-2, a.rect_.w(), 2};
		graphics::draw_rect(water_surface_rect, waterline_color, 255);
	}

	// draw the water edge and the deep, bottom-half-of-the-screen-filling underwater layer
	const int surface = a.rect_.y() + offset2;
	if(end_layer == max_zorder() && y + w >= surface) {
		const SDL_Rect r = {a.rect_.x(), std::max(surface, y), a.rect_.w(), a.rect_.h()};
		const SDL_Color deepwater_color = {91, 169, 143, 153};
		graphics::draw_rect(r, deepwater_color, 192);
		const SDL_Rect water_surface_rect = {a.rect_.x(), surface, a.rect_.w(), 2};
		graphics::draw_rect(water_surface_rect, waterline_color, 255);
	}
/*
	glDisable(GL_TEXTURE_2D);
	glLineWidth(3);
	glColor3ub(waterline_color.r, waterline_color.g, waterline_color.b);
	int start_line = -1;
	for(int n = 0; n != w; ++n) {
		if(solid_buf[n] && start_line == -1) {
			start_line = n;
		} else if((!solid_buf[n] || n == w-1) && start_line != -1) {
			glBegin(GL_LINES);
			glVertex3f(x + start_line, level_ + offset1, 0.0);
			glVertex3f(x + n + (solid_buf[n] ? 1 : 0), level_ + offset1, 0.0);
			glEnd();
			start_line = -1;
		}
	}
*/
	glColor4f(1.0, 1.0, 1.0, 1.0);
	glEnable(GL_TEXTURE_2D);

	return true;
}

void water::process(const level& lvl)
{
	foreach(area& a, areas_) {
		a.distortion_ = graphics::water_distortion(lvl.cycle(), a.rect_);
	}
}

SDL_Color water::get_color(int offset) const
{
	int index = 0;
	for(; index != positions_.size(); ++index) {
		//loop through till we find the first one with an offset greater than what we're looking at, and return its color
		if (positions_[index].offset > offset){
			return positions_[index].color;
		}
	}
	return graphics::color_blue();
}

int water::get_offset(int zorder) const
{
	/*int index = 0;
	 for(; index != positions_.size(); ++index) {
	 if(zorder == positions_[index].zorder) {
	 return positions_[index].offset;
	 }
	 
	 if(zorder < positions_[index].zorder) {
	 if(index == 0) {
	 return BadOffset;
	 }
	 
	 const float r1 = zorder - positions_[index-1].zorder;
	 const float r2 = positions_[index].zorder - zorder;
	 const float offset = (positions_[index-1].offset*r2 + positions_[index].offset*r1)/(r1+r2);
	 return static_cast<int>(offset);
	 }
	 }
	 
	 return BadOffset;*/
	
	
	//normalize the zorder value to a scale from 0 <-> 1, then linearly transform that to an offset
	float zorder_scale = max_zorder() - min_zorder();
	float zorder_current = zorder - min_zorder();
	float zorder_normalized = zorder_current / zorder_scale;
	float offset = zorder_normalized * (max_offset() - min_offset()) + min_offset();
	return static_cast<int>(offset);
}

int water::min_zorder() const
{
	if(positions_.empty()) {
		return BadOffset;
	}
	
	return positions_.front().zorder;
}

int water::max_zorder() const
{
	if(positions_.empty()) {
		return BadOffset;
	}
	
	return positions_.back().zorder;
}

int water::min_offset() const
{
	if(positions_.empty()) {
		return BadOffset;
	}
	
	return positions_.front().offset;
}

int water::max_offset() const
{
	if(positions_.empty()) {
		return BadOffset;
	}

	return positions_.back().offset;
}

void water::get_current(const entity& e, int* velocity_x, int* velocity_y) const
{
	if(velocity_x && current_x_formula_) {
		*velocity_x += current_x_formula_->execute(e).as_int();
	}

	if(velocity_y && current_y_formula_) {
		*velocity_y += current_y_formula_->execute(e).as_int();
	}
}

bool water::is_underwater(const rect& r, rect* result_water_area) const
{
	const point p((r.x() + r.x2())/2, (r.y() + r.y2())/2);
	foreach(const area& a, areas_) {
		if(point_in_rect(p, a.rect_)) {
			if(result_water_area) {
				*result_water_area = a.rect_;
			}
			return true;
		}
	}

	return false;
}
