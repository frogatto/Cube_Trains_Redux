#ifndef EDITOR_VARIABLE_INFO_HPP_INCLUDED
#define EDITOR_VARIABLE_INFO_HPP_INCLUDED

#include <boost/shared_ptr.hpp>

#include <string>
#include <vector>

#include "wml_node_fwd.hpp"

class editor_variable_info {
public:
	enum VARIABLE_TYPE { TYPE_INTEGER, XPOSITION, YPOSITION, TYPE_LEVEL, TYPE_LABEL, TYPE_TEXT };

	explicit editor_variable_info(wml::const_node_ptr node);

	wml::node_ptr write() const;

	const std::string& variable_name() const { return name_; }
	VARIABLE_TYPE type() const { return type_; }
	const std::string& info() const { return info_; }

private:
	std::string name_;
	VARIABLE_TYPE type_;
	std::string info_;
};

class editor_entity_info {
public:
	explicit editor_entity_info(wml::const_node_ptr node);

	wml::node_ptr write() const;

	const std::vector<editor_variable_info>& vars() const { return vars_; }
	const editor_variable_info* get_var_info(const std::string& var_name) const;
private:
	std::vector<editor_variable_info> vars_;
};

typedef boost::shared_ptr<editor_entity_info> editor_entity_info_ptr;
typedef boost::shared_ptr<const editor_entity_info> const_editor_entity_info_ptr;

#endif
