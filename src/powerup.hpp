#ifndef POWERUP_HPP_INCLUDED
#define POWERUP_HPP_INCLUDED

#include <boost/scoped_ptr.hpp>

#include <string>

#include "frame.hpp"
#include "powerup_fwd.hpp"
#include "wml_modify.hpp"
#include "wml_node_fwd.hpp"

class frame;

class powerup
{
public:
	explicit powerup(wml::const_node_ptr node);
	static void init(wml::const_node_ptr node);
	static const_powerup_ptr get(const std::string& id);

	const wml::modifier& modifier() const { return modifier_; }
	const frame& icon() const { return *icon_; }
	int duration() const { return duration_; }

private:
	wml::modifier modifier_;
	boost::scoped_ptr<frame> icon_;
	int duration_;
};

#endif
