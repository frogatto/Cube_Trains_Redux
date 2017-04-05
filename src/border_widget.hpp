#ifndef BORDER_WIDGET_HPP_INCLUDED
#define BORDER_WIDGET_HPP_INCLUDED

#include "graphics.hpp"
#include "color_utils.hpp"
#include "widget.hpp"

namespace gui {

//a widget which draws a border around another widget it holds as its child.
class border_widget : public widget
{
public:
	border_widget(widget_ptr child, graphics::color col, int border_size=2);
	border_widget(widget_ptr child, const SDL_Color& color, int border_size=2);
	border_widget(const variant& v, game_logic::formula_callable* e);
	void set_color(const graphics::color& col);
	virtual widget_ptr get_widget_by_id(const std::string& id);
protected:
	virtual void handle_draw() const;
	virtual void handle_process();
private:
	bool handle_event(const SDL_Event& event, bool claimed);

	widget_ptr child_;
	graphics::color color_;
	int border_size_;
};

typedef boost::intrusive_ptr<border_widget> border_widget_ptr;

}

#endif
