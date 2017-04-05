#ifndef CHECKBOX_HPP_INCLUDED
#define CHECKBOX_HPP_INCLUDED

#include "button.hpp"

#include <string>

namespace gui {

class checkbox : public virtual button
{
public:
	checkbox(const std::string& label, bool checked, boost::function<void(bool)> onclick, BUTTON_RESOLUTION button_resolution=BUTTON_SIZE_NORMAL_RESOLUTION);
	checkbox(widget_ptr label, bool checked, boost::function<void(bool)> onclick, BUTTON_RESOLUTION button_resolution=BUTTON_SIZE_NORMAL_RESOLUTION);
	checkbox(const variant& v, game_logic::formula_callable* e);
private:
	void on_click();

	std::string label_;
	widget_ptr label_widget_;
	boost::function<void(bool)> onclick_;
	bool checked_;
};

typedef boost::intrusive_ptr<checkbox> checkbox_ptr;

}

#endif
