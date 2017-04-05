#include <boost/bind.hpp>

#include "checkbox.hpp"
#include "graphical_font_label.hpp"
#include "grid_widget.hpp"
#include "image_widget.hpp"
#include "raster.hpp"

namespace gui {
namespace {

widget_ptr create_checkbox_widget(widget_ptr label, bool checked, BUTTON_RESOLUTION resolution) {
	grid_ptr g(new grid(2));
	g->set_hpad(12);
	g->add_col(widget_ptr(new gui_section_widget(checked ? "checkbox_ticked" : "checkbox_unticked", -1, -1, resolution == BUTTON_SIZE_NORMAL_RESOLUTION ? 1 : 2)));

	g->add_col(label);

	return g;
}

widget_ptr create_checkbox_widget(const std::string& text, bool checked, BUTTON_RESOLUTION resolution) {
	return create_checkbox_widget(widget_ptr(new graphical_font_label(text, "door_label", 2)), checked, resolution);
}
}

checkbox::checkbox(const std::string& label, bool checked, boost::function<void(bool)> onclick, BUTTON_RESOLUTION button_resolution)
  : button(create_checkbox_widget(label, checked, button_resolution), boost::bind(&checkbox::on_click, this), BUTTON_STYLE_NORMAL,button_resolution), label_(label), onclick_(onclick), checked_(checked)
{
	set_environment();
}

checkbox::checkbox(widget_ptr label, bool checked, boost::function<void(bool)> onclick, BUTTON_RESOLUTION button_resolution)
  : button(create_checkbox_widget(label, checked, button_resolution), boost::bind(&checkbox::on_click, this), BUTTON_STYLE_NORMAL,button_resolution), label_widget_(label), onclick_(onclick), checked_(checked)
{
	set_environment();
}

checkbox::checkbox(const variant& v, game_logic::formula_callable* e) : checked_(false), button(v,e)
{
}

void checkbox::on_click()
{
	checked_ = !checked_;
	const int w = width();
	const int h = height();
	if(label_widget_) {
		set_label(create_checkbox_widget(label_widget_, checked_, button_resolution()));
	} else {
		set_label(create_checkbox_widget(label_, checked_, button_resolution()));
	}
	set_dim(w, h);
	onclick_(checked_);
}

}
