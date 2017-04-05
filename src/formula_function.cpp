/* $Id: formula_function.cpp 25895 2008-04-17 18:57:13Z mordante $ */
/*
   Copyright (C) 2008 by David White <dave@whitevine.net>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2
   or at your option any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#include <boost/bind.hpp>
#include <iostream>
#include <stack>
#include <math.h>

#include "array_callable.hpp"
#include "asserts.hpp"
#include "compress.hpp"
#include "dialog.hpp"
#include "debug_console.hpp"
#include "foreach.hpp"
#include "formatter.hpp"
#include "formula.hpp"
#include "formula_callable.hpp"
#include "formula_callable_definition.hpp"
#include "formula_callable_utils.hpp"
#include "formula_function.hpp"
#include "formula_function_registry.hpp"
#include "geometry.hpp"
#include "hex_map.hpp"
#include "string_utils.hpp"
#include "unit_test.hpp"
#include "variant_callable.hpp"
#include "controls.hpp"
#include "pathfinding.hpp"
#include "preferences.hpp"
#include "level.hpp"
#include "json_parser.hpp"

#include "graphics.hpp"
#include "module.hpp"
#include <boost/regex.hpp>
#if defined(_WINDOWS)
#include <boost/math/special_functions/asinh.hpp>
#include <boost/math/special_functions/acosh.hpp>
#include <boost/math/special_functions/atanh.hpp>
#define asinh boost::math::asinh
#define acosh boost::math::acosh
#define atanh boost::math::atanh
#endif

#include "compat.hpp"

namespace {
	const std::string FunctionModule = "core";

	const float radians_to_degrees = 57.29577951308232087;
	const std::string EmptyStr;
}

namespace game_logic {

formula_expression::formula_expression(const char* name) : name_(name), begin_str_(EmptyStr.begin()), end_str_(EmptyStr.end()), ntimes_called_(0)
{}

void formula_expression::copy_debug_info_from(const formula_expression& o)
{
	set_debug_info(o.parent_formula_, o.begin_str_, o.end_str_);
}

void formula_expression::set_debug_info(const variant& parent_formula,
                                        std::string::const_iterator begin_str,
                                        std::string::const_iterator end_str)
{
	parent_formula_ = parent_formula;
	begin_str_ = begin_str;
	end_str_ = end_str;
	str_ = std::string(begin_str, end_str);
}

bool formula_expression::has_debug_info() const
{
	return parent_formula_.is_string() && parent_formula_.get_debug_info();
}

std::string pinpoint_location(variant v, std::string::const_iterator begin)
{
	return pinpoint_location(v, begin, begin);
}

std::string pinpoint_location(variant v, std::string::const_iterator begin,
                                         std::string::const_iterator end)
{
	std::string str(begin, end);
	if(!v.is_string() || !v.get_debug_info()) {
		return "Unknown location (" + str + ")\n";
	}

	int line_num = v.get_debug_info()->line;

	std::string::const_iterator begin_line = v.as_string().begin();
	while(std::find(begin_line, begin, '\n') != begin) {
		begin_line = std::find(begin_line, begin, '\n')+1;
		++line_num;
	}

	while(begin_line != begin && util::c_isspace(*begin_line)) {
		++begin_line;
	}

	std::string::const_iterator end_line = std::find(begin_line, v.as_string().end(), '\n');

	std::string line(begin_line, end_line);
	int pos = begin - begin_line;

	if(pos > 40) {
		line.erase(line.begin(), line.begin() + pos - 40);
		pos = 40;
		std::fill(line.begin(), line.begin() + 3, '.');
	}

	if(line.size() > 78) {
		line.resize(78);
		std::fill(line.end() - 3, line.end(), '.');
	}

	std::ostringstream s;
	s << "At " << *v.get_debug_info()->filename << " " << line_num << ":\n";
	s << line << "\n";
	for(int n = 0; n != pos; ++n) {
		s << " ";
	}
	s << "^";

	if(end > begin && pos + (end - begin) < line.size()) {
		for(int n = 0; n < (end - begin)-1; ++n) {
			s << "-";
		}
		s << "^";
	}

	s << "\n";
	return s.str();
}

std::string formula_expression::debug_pinpoint_location() const
{
	if(!has_debug_info()) {
		return "Unknown Location (" + str_ + ")\n";
	}

	return pinpoint_location(parent_formula_, begin_str_, end_str_);
}

variant formula_expression::execute_member(const formula_callable& variables, std::string& id) const
{
	formula::fail_if_static_context();
	ASSERT_LOG(false, "Trying to set illegal value: " << str_ << "\n" << debug_pinpoint_location());
	return variant();
}

namespace {

class ffl_cache : public formula_callable
{
public:
	explicit ffl_cache(int max_entries) : max_entries_(max_entries)
	{}
	const variant* get(const variant& key) const {
		std::map<variant, variant>::const_iterator i = cache_.find(key);
		if(i != cache_.end()) {
			return &i->second;
		} else {
			return NULL;
		}
	}

	void store(const variant& key, const variant& value) const {
		if(cache_.size() == max_entries_) {
			cache_.clear();
		}

		cache_[key] = value;
	}
private:
	variant get_value(const std::string& key) const {
		return variant();
	}

	mutable std::map<variant, variant> cache_;
	int max_entries_;
};

FUNCTION_DEF(create_cache, 0, 1, "create_cache(max_entries=4096): makes an FFL cache object")
	formula::fail_if_static_context();
	int max_entries = 4096;
	if(args().size() >= 1) {
		max_entries = args()[0]->evaluate(variables).as_int();
	}
	return variant(new ffl_cache(max_entries));
END_FUNCTION_DEF(create_cache)

FUNCTION_DEF(query_cache, 3, 3, "query_cache(ffl_cache, key, expr): ")
	const variant key = args()[1]->evaluate(variables);

	const ffl_cache* cache = args()[0]->evaluate(variables).try_convert<ffl_cache>();
	ASSERT_LOG(cache != NULL, "ILLEGAL CACHE ARGUMENT TO query_cache");
	
	const variant* result = cache->get(key);
	if(result != NULL) {
		return *result;
	}

	const variant value = args()[2]->evaluate(variables);
	cache->store(key, value);
	return value;
	
END_FUNCTION_DEF(query_cache)

	class if_function : public function_expression {
	public:
		explicit if_function(const args_list& args)
			: function_expression("if", args, 2, 3)
		{}

		expression_ptr optimize() const {
			variant v;
			if(args()[0]->can_reduce_to_variant(v)) {
				if(v.as_bool()) {
					return args()[1];
				} else {
					if(args().size() == 3) {
						return args()[2];
					} else {
						return expression_ptr(new variant_expression(variant()));
					}
				}
			}

			return expression_ptr();
		}

	private:
		variant execute(const formula_callable& variables) const {
			const int i = args()[0]->evaluate(variables).as_bool() ? 1 : 2;
			if(i >= args().size()) {
				return variant();
			}
			return args()[i]->evaluate(variables);
		}
	};

FUNCTION_DEF(bind_closure, 2, 2, "bind_closure(fn, obj): binds the given lambda fn to the given object closure")
	variant fn = args()[0]->evaluate(variables);
	return fn.bind_closure(args()[1]->evaluate(variables).as_callable());

END_FUNCTION_DEF(bind_closure)

FUNCTION_DEF(delay_until_end_of_loading, 1, 1, "delay_until_end_of_loading(string): delays evaluation of the enclosed until loading is finished")
	formula::fail_if_static_context();
	variant s = args()[0]->evaluate(variables);
	const_formula_ptr f(formula::create_optional_formula(s));
	if(!f) {
		return variant();
	}

	const_formula_callable_ptr callable(&variables);

	return variant::create_delayed(f, callable);
END_FUNCTION_DEF(delay_until_end_of_loading)

FUNCTION_DEF(eval, 1, 1, "eval(str): evaluate the given string as FFL")
	variant s = args()[0]->evaluate(variables);
	try {
		const assert_recover_scope recovery_scope;
		const_formula_ptr f(formula::create_optional_formula(s));
		if(!f) {
			return variant();
		}

		return f->execute(variables);
	} catch(type_error&) {
	} catch(validation_failure_exception&) {
	}
	std::cerr << "ERROR IN EVAL\n";
	return variant();
END_FUNCTION_DEF(eval)

FUNCTION_DEF(switch, 3, -1, "switch(value, case1, result1, case2, result2 ... casen, resultn, default) -> value: returns resultn where value = casen, or default otherwise.")
	variant var = args()[0]->evaluate(variables);
	for(size_t n = 1; n < args().size()-1; n += 2) {
		variant val = args()[n]->evaluate(variables);
		if(val == var) {
			return args()[n+1]->evaluate(variables);
		}
	}

	if((args().size()%2) == 0) {
		return args().back()->evaluate(variables);
	} else {
		return variant();
	}
END_FUNCTION_DEF(switch)

FUNCTION_DEF(query, 2, 2, "query(object, str): evaluates object.str")
	variant callable = args()[0]->evaluate(variables);
	variant str = args()[1]->evaluate(variables);
	return callable.as_callable()->query_value(str.as_string());
END_FUNCTION_DEF(query)


FUNCTION_DEF(abs, 1, 1, "abs(value) -> value: evaluates the absolute value of the value given")
	variant v = args()[0]->evaluate(variables);
	if(v.is_decimal()) {
		const decimal d = v.as_decimal();
		return variant(d >= 0 ? d : -d);
	} else {
		const int n = v.as_int();
		return variant(n >= 0 ? n : -n);
	}
END_FUNCTION_DEF(abs)

FUNCTION_DEF(sign, 1, 1, "sign(value) -> value: evaluates to 1 if positive, -1 if negative, and 0 if 0")
	const int n = args()[0]->evaluate(variables).as_int();
	if(n > 0) {
		return variant(1);
	} else if(n < 0) {
		return variant(-1);
	} else {
		return variant(0);
	}
END_FUNCTION_DEF(sign)

FUNCTION_DEF(median, 1, -1, "median(args...) -> value: evaluates to the median of the given arguments. If given a single argument list, will evaluate to the median of the member items.")
	if(args().size() == 3) {
		//special case for 3 arguments since it's a common case.
		variant a = args()[0]->evaluate(variables);
		variant b = args()[1]->evaluate(variables);
		variant c = args()[2]->evaluate(variables);
		if(a < b) {
			if(b < c) {
				return b;
			} else if(a < c) {
				return c;
			} else {
				return a;
			}
		} else {
			if(a < c) {
				return a;
			} else if(b < c) {
				return c;
			} else {
				return b;
			}
		}
	}

	std::vector<variant> items;
	if(args().size() != 1) {
		items.reserve(args().size());
	}

	for(size_t n = 0; n != args().size(); ++n) {
		const variant v = args()[n]->evaluate(variables);
		if(args().size() == 1 && v.is_list()) {
			items = v.as_list();
		} else {
			items.push_back(v);
		}
	}

	std::sort(items.begin(), items.end());
	if(items.empty()) {
		return variant();
	} else if(items.size()&1) {
		return items[items.size()/2];
	} else {
		return (items[items.size()/2-1] + items[items.size()/2])/variant(2);
	}
END_FUNCTION_DEF(median)

FUNCTION_DEF(min, 1, -1, "min(args...) -> value: evaluates to the minimum of the given arguments. If given a single argument list, will evaluate to the minimum of the member items.")

	bool found = false;
	variant res;
	for(size_t n = 0; n != args().size(); ++n) {
		const variant v = args()[n]->evaluate(variables);
		if(v.is_list() && args().size() == 1) {
			for(size_t m = 0; m != v.num_elements(); ++m) {
				if(!found || v[m] < res) {
					res = v[m];
					found = true;
				}
			}
		} else {
			if(!found || v < res) {
				res = v;
				found = true;
			}
		}
	}

	return res;
END_FUNCTION_DEF(min)

FUNCTION_DEF(max, 1, -1, "max(args...) -> value: evaluates to the maximum of the given arguments. If given a single argument list, will evaluate to the maximum of the member items.")

	bool found = false;
	variant res;
	for(size_t n = 0; n != args().size(); ++n) {
		const variant v = args()[n]->evaluate(variables);
		if(v.is_list() && args().size() == 1) {
			for(size_t m = 0; m != v.num_elements(); ++m) {
				if(!found || v[m] > res) {
					res = v[m];
					found = true;
				}
			}
		} else {
			if(!found || v > res) {
				res = v;
				found = true;
			}
		}
	}

	return res;
END_FUNCTION_DEF(max)

	UNIT_TEST(min_max_decimal) {
		CHECK(game_logic::formula(variant("max(1,1.4)")).execute() == game_logic::formula(variant("1.4")).execute(), "test failed");
	}

FUNCTION_DEF(keys, 1, 1, "keys(map) -> list: gives the keys for a map")
	const variant map = args()[0]->evaluate(variables);
	if(map.is_callable()) {
		std::vector<variant> v;
		const std::vector<formula_input> inputs = map.as_callable()->inputs();
		foreach(const formula_input& in, inputs) {
			v.push_back(variant(in.name));
		}

		return variant(&v);
	}

	return map.get_keys();
END_FUNCTION_DEF(keys)

FUNCTION_DEF(values, 1, 1, "values(map) -> list: gives the values for a map")
	const variant map = args()[0]->evaluate(variables);
	return map.get_values();
END_FUNCTION_DEF(values)

FUNCTION_DEF(wave, 1, 1, "wave(int) -> int: a wave with a period of 1000 and height of 1000")
	const int value = args()[0]->evaluate(variables).as_int()%1000;
	const double angle = 2.0*3.141592653589*(static_cast<double>(value)/1000.0);
	return variant(static_cast<int>(sin(angle)*1000.0));
END_FUNCTION_DEF(wave)

FUNCTION_DEF(decimal, 1, 1, "decimal(value) -> decimal: converts the value to a decimal")
	return variant(args()[0]->evaluate(variables).as_decimal());
END_FUNCTION_DEF(decimal)

FUNCTION_DEF(integer, 1, 1, "integer(value) -> int: converts the value to an integer")
	return variant(args()[0]->evaluate(variables).as_int());
END_FUNCTION_DEF(integer)

FUNCTION_DEF(sin, 1, 1, "sin(x): Standard sine function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(sin(angle/radians_to_degrees)));
END_FUNCTION_DEF(sin)

FUNCTION_DEF(cos, 1, 1, "cos(x): Standard cosine function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(cos(angle/radians_to_degrees)));
END_FUNCTION_DEF(cos)

FUNCTION_DEF(tan, 1, 1, "tan(x): Standard tangent function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(tan(angle/radians_to_degrees)));
END_FUNCTION_DEF(tan)

FUNCTION_DEF(asin, 1, 1, "asin(x): Standard arc sine function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(asin(angle/radians_to_degrees)));
END_FUNCTION_DEF(asin)

FUNCTION_DEF(acos, 1, 1, "acos(x): Standard arc cosine function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(acos(angle/radians_to_degrees)));
END_FUNCTION_DEF(acos)

FUNCTION_DEF(atan, 1, 1, "atan(x): Standard arc tangent function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(atan(angle/radians_to_degrees)));
END_FUNCTION_DEF(atan)

FUNCTION_DEF(sinh, 1, 1, "sinh(x): Standard hyperbolic sine function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(sinh(angle)));
END_FUNCTION_DEF(sinh)

FUNCTION_DEF(cosh, 1, 1, "cosh(x): Standard hyperbolic cosine function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(cosh(angle)));
END_FUNCTION_DEF(cosh)

FUNCTION_DEF(tanh, 1, 1, "tanh(x): Standard hyperbolic tangent function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(tanh(angle)));
END_FUNCTION_DEF(tanh)

FUNCTION_DEF(asinh, 1, 1, "asinh(x): Standard arc hyperbolic sine function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(asinh(angle)));
END_FUNCTION_DEF(asinh)

FUNCTION_DEF(acosh, 1, 1, "acosh(x): Standard arc hyperbolic cosine function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(acosh(angle)));
END_FUNCTION_DEF(acosh)

FUNCTION_DEF(atanh, 1, 1, "atanh(x): Standard arc hyperbolic tangent function.")
	const float angle = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(static_cast<decimal>(atanh(angle)));
END_FUNCTION_DEF(atanh)

FUNCTION_DEF(sqrt, 1, 1, "sqrt(x): Returns the square root of x.")
	const double value = args()[0]->evaluate(variables).as_decimal().as_float();
	return variant(decimal(sqrt(value)));
END_FUNCTION_DEF(sqrt)

FUNCTION_DEF(angle, 4, 4, "angle(x1, y1, x2, y2) -> int: Returns the angle, from 0°, made by the line described by the two points (x1, y1) and (x2, y2).")
	const float a = args()[0]->evaluate(variables).as_int();
	const float b = args()[1]->evaluate(variables).as_int();
	const float c = args()[2]->evaluate(variables).as_int();
	const float d = args()[3]->evaluate(variables).as_int();
	return variant(static_cast<int>(round((atan2(a-c, b-d)*radians_to_degrees+90)*VARIANT_DECIMAL_PRECISION)*-1), variant::DECIMAL_VARIANT);
END_FUNCTION_DEF(angle)

FUNCTION_DEF(angle_delta, 2, 2, "angle_delta(a, b) -> int: Given two angles, returns the smallest rotation needed to make a equal to b.")
	int a = args()[0]->evaluate(variables).as_int();
	int b = args()[1]->evaluate(variables).as_int();
	while(abs(a - b) > 180) {
		if(a < b) {
			a += 360;
		} else {
			b += 360;
		}
	}

	return variant(b - a);
END_FUNCTION_DEF(angle_delta)

FUNCTION_DEF(orbit, 4, 4, "orbit(x, y, angle, dist) -> [x,y]: Returns the point as a list containing an x/y pair which is dist away from the point as defined by x and y passed in, at the angle passed in.")
	const float x = args()[0]->evaluate(variables).as_decimal().as_float();
	const float y = args()[1]->evaluate(variables).as_decimal().as_float();
	const float ang = args()[2]->evaluate(variables).as_decimal().as_float();
	const float dist = args()[3]->evaluate(variables).as_decimal().as_float();
	
	const float u = (dist * cos(ang/radians_to_degrees)) + x;   //TODO Find out why whole number decimals are returned.
	const float v = (dist * sin(ang/radians_to_degrees)) + y;

	std::vector<variant> result;
	result.reserve(2);
	result.push_back(variant(decimal(u)));
	result.push_back(variant(decimal(v)));
	
	return variant(&result);
END_FUNCTION_DEF(orbit)

FUNCTION_DEF(regex_replace, 3, 3, "regex_replace(string, string, string) -> string: Unknown.")
	const std::string str = args()[0]->evaluate(variables).as_string();
	const boost::regex re(args()[1]->evaluate(variables).as_string());
	const std::string value = args()[2]->evaluate(variables).as_string();
	return variant(boost::regex_replace(str, re, value));
END_FUNCTION_DEF(regex_replace)

FUNCTION_DEF(regex_match, 2, 2, "regex_match(string, re_string) -> string: returns null if not found, else returns the whole string or a list of sub-strings depending on whether blocks were demarcated.")
	const std::string str = args()[0]->evaluate(variables).as_string();
	const boost::regex re(args()[1]->evaluate(variables).as_string());
	 boost::match_results<std::string::const_iterator> m;
	if(boost::regex_match(str, m, re) == false) {
		return variant();
	}
	if(m.size() == 1) {
		return variant(std::string(m[0].first, m[0].second));
	} 
	std::vector<variant> v;
	for(size_t i = 1; i < m.size(); i++) {
		v.push_back(variant(std::string(m[i].first, m[i].second)));
	}
	return variant(&v);
END_FUNCTION_DEF(regex_match)

FUNCTION_DEF(fold, 2, 3, "fold(list, expr, [default]) -> value")
	variant list = args()[0]->evaluate(variables);
	const int size = list.num_elements();
	if(size == 0) {
		if(args().size() >= 3) {
			return args()[2]->evaluate(variables);
		} else {
			return variant();
		}
	} else if(size == 1) {
		return list[0];
	}

	boost::intrusive_ptr<map_formula_callable> callable(new map_formula_callable(&variables));
	variant& a = callable->add_direct_access("a");
	variant& b = callable->add_direct_access("b");
	a = list[0];
	for(int n = 1; n < list.num_elements(); ++n) {
		b = list[n];
		a = args()[1]->evaluate(*callable);
	}

	return a;
END_FUNCTION_DEF(fold)

FUNCTION_DEF(unzip, 1, 1, "unzip(list of lists) -> list of lists: Converts [[1,4],[2,5],[3,6]] -> [[1,2,3],[4,5,6]]")
	variant item1 = args()[0]->evaluate(variables);
	ASSERT_LOG(item1.is_list(), "unzip function arguments must be a list");

	// Calculate breadth and depth of new list.
	const int depth = item1.num_elements();
	size_t breadth = 0;
	for(size_t n = 0; n < item1.num_elements(); ++n) {
		ASSERT_LOG(item1[n].is_list(), "Item " << n << " on list isn't list");
		breadth = std::max(item1[n].num_elements(), breadth);
	}

	std::vector<std::vector<variant> > v;
	for(size_t n = 0; n < breadth; ++n) {
		std::vector<variant> e1;
		e1.resize(depth);
		v.push_back(e1);
	}

	for(size_t n = 0; n < item1.num_elements(); ++n) {
		for(size_t m = 0; m < item1[n].num_elements(); ++m) {
			v[m][n] = item1[n][m];
		}
	}

	std::vector<variant> vl;
	for(size_t n = 0; n < v.size(); ++n) {
		vl.push_back(variant(&v[n]));
	}
	return variant(&vl);
END_FUNCTION_DEF(unzip)

FUNCTION_DEF(zip, 3, 3, "zip(list1, list2, expr) -> list")
	map_formula_callable_ptr callable(new map_formula_callable(&variables));
	variant& a = callable->add_direct_access("a");
	variant& b = callable->add_direct_access("b");
	variant item1 = args()[0]->evaluate(variables);
	variant item2 = args()[1]->evaluate(variables);

	ASSERT_LOG(item1.type() == item2.type(), "zip function arguments must both be the same type.");
	ASSERT_LOG(item1.is_list() || item1.is_map(), "zip function arguments must be either lists or maps");
	const int size = std::min(item1.num_elements(), item2.num_elements());

	if(item1.is_list()) {
		std::vector<variant> retList;
		// is list
		if(size != 0) {
			for(int n = 0; n < size; ++n) {
				a = item1[n];
				b = item2[n];
				retList.push_back(args()[2]->evaluate(*callable));
			}
		}
		return variant(&retList);
	} else {
		std::map<variant,variant> retMap(item1.as_map());
		variant keys = item2.get_keys();
		for(int n = 0; n != keys.num_elements(); n++) {
			if(retMap[keys[n]].is_null() == false) {
				a = retMap[keys[n]];
				b = item2[keys[n]];
				retMap[keys[n]] = args()[2]->evaluate(*callable);
			} else {
				retMap[keys[n]] = item2[keys[n]];
			}
		}
		return variant(&retMap);
	}
	return variant();
END_FUNCTION_DEF(zip)

FUNCTION_DEF(float_array, 1, 2, "float_array(list, (opt) num_elements) -> callable: Converts a list of floating point values into an efficiently accessible object.")
	game_logic::formula::fail_if_static_context();
	variant f = args()[0]->evaluate(variables);
	int num_elems = args().size() == 1 ? 1 : args()[1]->evaluate(variables).as_int();
	std::vector<GLfloat> floats;
	for(size_t n = 0; n < f.num_elements(); ++n) {
		floats.push_back(GLfloat(f[n].as_decimal().as_float()));
	}
	return variant(new float_array_callable(&floats, num_elems));
END_FUNCTION_DEF(float_array)

FUNCTION_DEF(short_array, 1, 2, "short_array(list) -> callable: Converts a list of integer values into an efficiently accessible object.")
	game_logic::formula::fail_if_static_context();
	variant s = args()[0]->evaluate(variables);
	int num_elems = args().size() == 1 ? 1 : args()[1]->evaluate(variables).as_int();
	std::vector<GLshort> shorts;
	for(size_t n = 0; n < s.num_elements(); ++n) {
		shorts.push_back(GLshort(s[n].as_int()));
	}
	return variant(new short_array_callable(&shorts, num_elems));
END_FUNCTION_DEF(short_array)

/* XXX Krista to be reworked
FUNCTION_DEF(update_controls, 1, 1, "update_controls(map) : Updates the controls based on a list of id:string, pressed:bool pairs")
	const variant map = args()[0]->evaluate(variables);
	foreach(const variant_pair& p, map.as_map()) {
		std::cerr << "Button: " << p.first.as_string() << " " << (p.second.as_bool() ? "Pressed" : "Released") << std::endl;
		controls::update_control_state(p.first.as_string(), p.second.as_bool());
	}
	return variant();
END_FUNCTION_DEF(update_controls)

FUNCTION_DEF(map_controls, 1, 1, "map_controls(map) : Creates or updates the mapping on controls to keys")
	const variant map = args()[0]->evaluate(variables);
	foreach(const variant_pair& p, map.as_map()) {
		controls::set_mapped_key(p.first.as_string(), static_cast<SDLKey>(p.second.as_int()));
	}
	return variant();
END_FUNCTION_DEF(map_controls)*/

FUNCTION_DEF(directed_graph, 2, 2, "directed_graph(list_of_vertexes, adjacent_expression) -> a directed graph")
	variant vertices = args()[0]->evaluate(variables);
	pathfinding::graph_edge_list edges;
	
	std::vector<variant> vertex_list;
	boost::intrusive_ptr<map_formula_callable> callable(new map_formula_callable(&variables));
	variant& a = callable->add_direct_access("v");
	foreach(variant v, vertices.as_list()) {
		a = v;
		edges[v] = args()[1]->evaluate(*callable).as_list();
		vertex_list.push_back(v);
	}
	pathfinding::directed_graph* dg = new pathfinding::directed_graph(&vertex_list, &edges);
	return variant(dg);
END_FUNCTION_DEF(directed_graph)

FUNCTION_DEF(weighted_graph, 2, 2, "weighted_graph(directed_graph, weight_expression) -> a weighted directed graph")
	variant graph = args()[0]->evaluate(variables);
	pathfinding::directed_graph_ptr dg = boost::intrusive_ptr<pathfinding::directed_graph>(graph.try_convert<pathfinding::directed_graph>());
	ASSERT_LOG(dg, "Directed graph given is not of the correct type.");
	pathfinding::edge_weights w;
	boost::intrusive_ptr<map_formula_callable> callable(new map_formula_callable(&variables));
	variant& a = callable->add_direct_access("a");
	variant& b = callable->add_direct_access("b");
	for(pathfinding::graph_edge_list::const_iterator edges = dg->get_edges()->begin(); 
		edges != dg->get_edges()->end(); 
		edges++) {
		foreach(const variant e2, edges->second) {
			a = edges->first;
			b = e2;
			w[pathfinding::graph_edge(edges->first, e2)] = args()[1]->evaluate(*callable).as_decimal();
		}
	}
	return variant(new pathfinding::weighted_directed_graph(dg, &w));
END_FUNCTION_DEF(weighted_graph)

FUNCTION_DEF(a_star_search, 4, 4, "a_star_search(weighted_directed_graph, src_node, dst_node, heuristic) -> A list of nodes which represents the 'best' path from src_node to dst_node.")
	variant graph = args()[0]->evaluate(variables);
	pathfinding::weighted_directed_graph_ptr wg = graph.try_convert<pathfinding::weighted_directed_graph>();
	ASSERT_LOG(wg, "Weighted graph given is not of the correct type.");
	variant src_node = args()[1]->evaluate(variables);
	variant dst_node = args()[2]->evaluate(variables);
	expression_ptr heuristic = args()[3];
	boost::intrusive_ptr<map_formula_callable> callable(new map_formula_callable(&variables));
	return pathfinding::a_star_search(wg, src_node, dst_node, heuristic, callable);
END_FUNCTION_DEF(a_star_search)

FUNCTION_DEF(path_cost_search, 3, 3, "cost_search(weighted_directed_graph, src_node, max_cost) -> A list of all possible points reachable from src_node within max_cost.")
	variant graph = args()[0]->evaluate(variables);
	pathfinding::weighted_directed_graph_ptr wg = graph.try_convert<pathfinding::weighted_directed_graph>();
	ASSERT_LOG(wg, "Weighted graph given is not of the correct type.");
	variant src_node = args()[1]->evaluate(variables);
	decimal max_cost(args()[2]->evaluate(variables).as_decimal());
	return pathfinding::path_cost_search(wg, src_node, max_cost);
END_FUNCTION_DEF(path_cost_search)

FUNCTION_DEF(create_graph_from_level, 1, 3, "create_graph_from_level(level, (optional) tile_size_x, (optional) tile_size_y) -> directed graph : Creates a directed graph based on the current level.")
	int tile_size_x = TileSize;
	int tile_size_y = TileSize;
	if(args().size() == 2) {
		tile_size_y = tile_size_x = args()[1]->evaluate(variables).as_int();
	} else if(args().size() == 3) {
		tile_size_x = args()[1]->evaluate(variables).as_int();
		tile_size_y = args()[2]->evaluate(variables).as_int();
	}
	ASSERT_LOG((tile_size_x%2)==0 && (tile_size_y%2)==0, "The tile_size_x and tile_size_y values *must* be even. (" << tile_size_x << "," << tile_size_y << ")");
	variant curlevel = args()[0]->evaluate(variables);
	level_ptr lvl = curlevel.try_convert<level>();
	ASSERT_LOG(lvl, "The level parameter passed to the function was couldn't be converted.");
	rect b = lvl->boundaries();
	b.from_coordinates(b.x() - b.x()%tile_size_x, 
		b.y() - b.y()%tile_size_y, 
		b.x2()+(tile_size_x-b.x2()%tile_size_x), 
		b.y2()+(tile_size_y-b.y2()%tile_size_y));

	pathfinding::graph_edge_list edges;
	std::vector<variant> vertex_list;
	const rect& b_rect = level::current().boundaries();

	for(int y = b.y(); y < b.y2(); y += tile_size_y) {
		for(int x = b.x(); x < b.x2(); x += tile_size_x) {
			if(!lvl->solid(x, y, tile_size_x, tile_size_y)) {
				variant l(pathfinding::point_as_variant_list(point(x,y)));
				vertex_list.push_back(l);
				std::vector<variant> e;
				point po(x,y);
				foreach(const point& p, pathfinding::get_neighbours_from_rect(po, tile_size_x, tile_size_y, b_rect)) {
					if(!lvl->solid(p.x, p.y, tile_size_x, tile_size_y)) {
						e.push_back(pathfinding::point_as_variant_list(p));
					}
				}
				edges[l] = e;
			}
		}
	}
	return variant(new pathfinding::directed_graph(&vertex_list, &edges));
END_FUNCTION_DEF(create_graph_from_level)

FUNCTION_DEF(plot_path, 6, 9, "plot_path(level, from_x, from_y, to_x, to_y, heuristic, (optional) weight_expr, (optional) tile_size_x, (optional) tile_size_y) -> list : Returns a list of points to get from (from_x, from_y) to (to_x, to_y)")
	int tile_size_x = TileSize;
	int tile_size_y = TileSize;
	expression_ptr weight_expr = expression_ptr();
	variant curlevel = args()[0]->evaluate(variables);
	level_ptr lvl = curlevel.try_convert<level>();
	if(args().size() > 6) {
		weight_expr = args()[6];
	}
	if(args().size() == 8) {
		tile_size_y = tile_size_x = args()[6]->evaluate(variables).as_int();
	} else if(args().size() == 9) {
		tile_size_x = args()[6]->evaluate(variables).as_int();
		tile_size_y = args()[7]->evaluate(variables).as_int();
	}
	ASSERT_LOG((tile_size_x%2)==0 && (tile_size_y%2)==0, "The tile_size_x and tile_size_y values *must* be even. (" << tile_size_x << "," << tile_size_y << ")");
	point src(args()[1]->evaluate(variables).as_int(), args()[2]->evaluate(variables).as_int());
	point dst(args()[3]->evaluate(variables).as_int(), args()[4]->evaluate(variables).as_int());
	expression_ptr heuristic = args()[4];
	boost::intrusive_ptr<map_formula_callable> callable(new map_formula_callable(&variables));
	return variant(pathfinding::a_star_find_path(lvl, src, dst, heuristic, weight_expr, callable, tile_size_x, tile_size_y));
END_FUNCTION_DEF(plot_path)

namespace {
class variant_comparator : public formula_callable {
	expression_ptr expr_;
	const formula_callable* fallback_;
	mutable variant a_, b_;
	variant get_value(const std::string& key) const {
		if(key == "a") {
			return a_;
		} else if(key == "b") {
			return b_;
		} else {
			return fallback_->query_value(key);
		}
	}

	variant get_value_by_slot(int slot) const {
		return fallback_->query_value_by_slot(slot);
	}

	void get_inputs(std::vector<formula_input>* inputs) const {
		fallback_->get_inputs(inputs);
	}
public:
	variant_comparator(const expression_ptr& expr, const formula_callable& fallback) : formula_callable(false), expr_(expr), fallback_(&fallback)
	{}

	bool operator()(const variant& a, const variant& b) const {
		a_ = a;
		b_ = b;
		return expr_->evaluate(*this).as_bool();
	}
};
}

FUNCTION_DEF(sort, 1, 2, "sort(list, criteria): Returns a nicely-ordered list. If you give it an optional formula such as 'a>b' it will sort it according to that. This example favours larger numbers first instead of the default of smaller numbers first.")
	variant list = args()[0]->evaluate(variables);
	std::vector<variant> vars;
	vars.reserve(list.num_elements());
	for(size_t n = 0; n != list.num_elements(); ++n) {
		vars.push_back(list[n]);
	}

	if(args().size() == 1) {
		std::sort(vars.begin(), vars.end());
	} else {
		std::sort(vars.begin(), vars.end(), variant_comparator(args()[1], variables));
	}

	return variant(&vars);
END_FUNCTION_DEF(sort)

FUNCTION_DEF(shuffle, 1, 1, "shuffle(list) - Returns a shuffled version of the list. Like shuffling cards.")
	variant list = args()[0]->evaluate(variables);
	boost::intrusive_ptr<float_array_callable> f = list.try_convert<float_array_callable>();
	if(f != NULL) {
		std::vector<GLfloat> floats(f->floats().begin(), f->floats().end());
		std::random_shuffle(floats.begin(), floats.end());
		return variant(new float_array_callable(&floats));
	}
	
	boost::intrusive_ptr<short_array_callable> s = list.try_convert<short_array_callable>();
	if(s != NULL) {
		std::vector<GLshort> shorts(s->shorts().begin(), s->shorts().end());
		std::random_shuffle(shorts.begin(), shorts.end());
		return variant(new short_array_callable(&shorts));
	}

	std::vector<variant> vars;
	vars.reserve(list.num_elements());
	for(size_t n = 0; n != list.num_elements(); ++n) {
		vars.push_back(list[n]);
	}

	std::random_shuffle(vars.begin(), vars.end());

	return variant(&vars);
END_FUNCTION_DEF(shuffle)
	
namespace {
	void flatten_items( variant items, std::vector<variant>* output){
		for(size_t n = 0; n != items.num_elements(); ++n) {
			
			if( items[n].is_list() ){
				flatten_items(items[n], output);
			} else {
				output->push_back(items[n]);
			}
			
		}
	}
	
}

FUNCTION_DEF(flatten, 1, 1, "flatten(list): Returns a list with a depth of 1 containing the elements of any list passed in.")
	variant input = args()[0]->evaluate(variables);
	std::vector<variant> output;
	flatten_items(input, &output);
	return variant(&output);
END_FUNCTION_DEF(flatten)
	
class map_callable : public formula_callable {
	public:
		explicit map_callable(const formula_callable& backup)
		: backup_(backup)
		{}

		void set(const variant& v, int i)
		{
			value_ = v;
			index_ = i;
		}
	private:
		variant get_value(const std::string& key) const {
			if(key == "value") {
				return value_;
			} else if(key == "index") {
				return variant(index_);
			} else if(key == "context") {
				return variant(&backup_);
			} else {
				return backup_.query_value(key);
			}
		}

		variant get_value_by_slot(int slot) const {
			return backup_.query_value_by_slot(slot);
		}

		const formula_callable& backup_;
		variant value_;
		int index_;
};

class filter_function : public function_expression {
public:
	explicit filter_function(const args_list& args)
		: function_expression("filter", args, 2, 3)
	{
		if(args.size() == 3) {
			args[1]->is_identifier(&identifier_);
		}
	}
private:
	std::string identifier_;
	variant execute(const formula_callable& variables) const {
		std::vector<variant> vars;
		const variant items = args()[0]->evaluate(variables);
		if(args().size() == 2) {

			if(items.is_map()) {
				map_formula_callable_ptr callable(new map_formula_callable(&variables));
				std::map<variant,variant> m;
				callable->add("context", variant(&variables));
				foreach(const variant_pair& p, items.as_map()) {
					callable->add("key", p.first);
					callable->add("value", p.second);
					const variant val = args().back()->evaluate(*callable);
					if(val.as_bool()) {
						m[p.first] = p.second;
					}
				}

				return variant(&m);
			} else {
				boost::intrusive_ptr<map_callable> callable(new map_callable(variables));
				for(size_t n = 0; n != items.num_elements(); ++n) {
					callable->set(items[n], n);
					const variant val = args().back()->evaluate(*callable);
					if(val.as_bool()) {
						vars.push_back(items[n]);
					}
				}
			}
		} else {
			map_formula_callable* self_callable = new map_formula_callable;
			formula_callable_ptr callable(self_callable);
			self_callable->add("context", variant(&variables));
			const std::string self = identifier_.empty() ? args()[1]->evaluate(variables).as_string() : identifier_;

			variant& item_var = self_callable->add_direct_access(self);
			variant& index_var = self_callable->add_direct_access("index");
			for(size_t n = 0; n != items.num_elements(); ++n) {
				item_var = items[n];
				index_var = variant(unsigned(n));
				formula_callable_ptr callable_with_backup(new formula_variant_callable_with_backup(items[n], variables));
				formula_callable_ptr callable_ptr(new formula_callable_with_backup(*self_callable, *callable_with_backup));
				const variant val = args()[2]->evaluate(*callable_ptr);
				if(val.as_bool()) {
					vars.push_back(items[n]);
				}
			}
		}

		return variant(&vars);
	}
};
	
FUNCTION_DEF(mapping, -1, -1, "mapping(x): Turns the args passed in into a map. The first arg is a key, the second a value, the third a key, the fourth a value and so on and so forth.")
	map_formula_callable* callable = new map_formula_callable;
	for(size_t n = 0; n < args().size()-1; n += 2) {
		callable->add(args()[n]->evaluate(variables).as_string(),
					args()[n+1]->evaluate(variables));
	}
	
	return variant(callable);
END_FUNCTION_DEF(mapping)

class find_function : public function_expression {
public:
	explicit find_function(const args_list& args)
		: function_expression("find", args, 2, 3)
	{
		if(args.size() == 3) {
			args[1]->is_identifier(&identifier_);
		}
	}

private:
	std::string identifier_;
	variant execute(const formula_callable& variables) const {
		const variant items = args()[0]->evaluate(variables);

		if(args().size() == 2) {
			boost::intrusive_ptr<map_callable> callable(new map_callable(variables));
			for(size_t n = 0; n != items.num_elements(); ++n) {
				callable->set(items[n], n);
				const variant val = args().back()->evaluate(*callable);
				if(val.as_bool()) {
					return items[n];
				}
			}
		} else {
			map_formula_callable* self_callable = new map_formula_callable;
			formula_callable_ptr callable(self_callable);
			self_callable->add("context", variant(&variables));

			const std::string self = identifier_.empty() ? args()[1]->evaluate(variables).as_string() : identifier_;
			for(size_t n = 0; n != items.num_elements(); ++n) {
				self_callable->add(self, items[n]);

				boost::intrusive_ptr<formula_variant_callable_with_backup> callable_backup(new formula_variant_callable_with_backup(items[n], variables));

				formula_callable_ptr callable(new formula_callable_with_backup(*self_callable, *callable_backup));
				const variant val = args().back()->evaluate(*callable);
				if(val.as_bool()) {
					return items[n];
				}
			}
		}

		return variant();
	}
};

	class transform_callable : public formula_callable {
	public:
		explicit transform_callable(const formula_callable& backup)
		: backup_(backup)
		{}

		void set(const variant& v, const variant& i)
		{
			value_ = v;
			index_ = i;
		}
	private:
		variant get_value(const std::string& key) const {
			if(key == "v") {
				return value_;
			} else if(key == "i") {
				return index_;
			} else {
				return backup_.query_value(key);
			}
		}

		variant get_value_by_slot(int slot) const {
			return backup_.query_value_by_slot(slot);
		}

		const formula_callable& backup_;
		variant value_, index_;
	};

FUNCTION_DEF(transform, 2, 2, "transform(list,ffl): calls the ffl for each item on the given list, returning a list of the results. Inside the transform v is the value of the list item and i is the index. e.g. transform([1,2,3], v+2) = [3,4,5] and transform([1,2,3], i) = [0,1,2]")
	std::vector<variant> vars;
	const variant items = args()[0]->evaluate(variables);

	vars.reserve(items.num_elements());

	transform_callable* callable = new transform_callable(variables);
	variant v(callable);

	const int nitems = items.num_elements();
	for(size_t n = 0; n != nitems; ++n) {
		callable->set(items[n], variant(unsigned(n)));
		const variant val = args().back()->evaluate(*callable);
		vars.push_back(val);
	}

	return variant(&vars);
END_FUNCTION_DEF(transform)

namespace {
void visit_objects(variant v, std::vector<variant>& res) {
	if(v.is_map()) {
		res.push_back(v);
		foreach(const variant_pair& value, v.as_map()) {
			visit_objects(value.second, res);
		}
	} else if(v.is_list()) {
		foreach(const variant& value, v.as_list()) {
			visit_objects(value, res);
		}
	} else if(v.try_convert<variant_callable>()) {
		res.push_back(v);
		variant keys = v.try_convert<variant_callable>()->get_value().get_keys();
		foreach(variant k, keys.as_list()) {
			visit_objects(v.try_convert<variant_callable>()->query_value(k.as_string()), res);
		}
	}
}
}

class visit_objects_function : public function_expression 
{
public:
	explicit visit_objects_function(const args_list& args)
		: function_expression("visit_objects", args, 1, 1)
	{}
private:
	variant execute(const formula_callable& variables) const {
		const variant v = args()[0]->evaluate(variables);
		std::vector<variant> result;
		visit_objects(v, result);
		return variant(&result);
	}
};

FUNCTION_DEF(choose, 1, 2, "choose(list, (optional)scoring_expr) -> value: choose an item from the list according to which scores the highest according to the scoring expression, or at random by default.")

	if(args().size() == 1) {
		formula::fail_if_static_context();
	}

	const variant items = args()[0]->evaluate(variables);
	int max_index = -1;
	variant max_value;
	boost::intrusive_ptr<map_callable> callable(new map_callable(variables));
	for(size_t n = 0; n != items.num_elements(); ++n) {
		variant val;
		
		if(args().size() >= 2) {
			callable->set(items[n], n);
			val = args()[1]->evaluate(*callable);
		} else {
			val = variant(rand());
		}

		if(max_index == -1 || val > max_value) {
			max_index = n;
			max_value = val;
		}
	}

	if(max_index == -1) {
		return variant();
	} else {
		return items[max_index];
	}
END_FUNCTION_DEF(choose)

class map_function : public function_expression {
public:
	explicit map_function(const args_list& args)
		: function_expression("map", args, 2, 3)
	{
		if(args.size() == 3) {
			args[1]->is_identifier(&identifier_);
		}
	}
private:
	std::string identifier_;

	variant execute(const formula_callable& variables) const {
		std::vector<variant> vars;
		const variant items = args()[0]->evaluate(variables);

		vars.reserve(items.num_elements());

		if(args().size() == 2) {

			if(items.is_map()) {
				map_formula_callable_ptr callable(new map_formula_callable(&variables));
				callable->add("context", variant(&variables));
				int index = 0;
				foreach(const variant_pair& p, items.as_map()) {
					callable->add("key", p.first);
					callable->add("value", p.second);
					callable->add("index", variant(index));
					const variant val = args().back()->evaluate(*callable);
					vars.push_back(val);
					++index;
				}
			} else {
				boost::intrusive_ptr<map_callable> callable(new map_callable(variables));
				for(size_t n = 0; n != items.num_elements(); ++n) {
					callable->set(items[n], n);
					const variant val = args().back()->evaluate(*callable);
					vars.push_back(val);
				}
			}
		} else {
			static const std::string index_str = "index";
			static const std::string context_str = "context";
			map_formula_callable* self_callable = new map_formula_callable;
			formula_callable_ptr callable_ref(self_callable);
			self_callable->add(context_str, variant(&variables));
			const std::string self = identifier_.empty() ? args()[1]->evaluate(variables).as_string() : identifier_;

			variant& self_variant = self_callable->add_direct_access(self);

			//the variant representing the index we are currently at.
			variant& index_variant = self_callable->add_direct_access(index_str);
			index_variant = variant(0);

			formula_callable_ptr callable_backup(new formula_callable_with_backup(*self_callable, variables));

			const int nelements = items.num_elements();
			for(int& n = index_variant.int_addr(); n != nelements; ++n) {
				self_variant = items[n];
				vars.push_back(args().back()->evaluate(*callable_backup));
			}
		}

		return variant(&vars);
	}
};

FUNCTION_DEF(sum, 1, 2, "sum(list[, counter]): Adds all elements of the list together. If counter is supplied, all elements of the list are added to the counter instead of to 0.")
	variant res(0);
	const variant items = args()[0]->evaluate(variables);
	if(args().size() >= 2) {
		res = args()[1]->evaluate(variables);
	}
	for(size_t n = 0; n != items.num_elements(); ++n) {
		res = res + items[n];
	}

	return res;
END_FUNCTION_DEF(sum)

FUNCTION_DEF(range, 1, 3, "range([start, ]finish[, step]): Returns a list containing all numbers smaller than the finish value and and larger than or equal to the start value. The start value defaults to 0.")
	int start = args().size() > 1 ? args()[0]->evaluate(variables).as_int() : 0;
	int end = args()[args().size() > 1 ? 1 : 0]->evaluate(variables).as_int();
	int step = args().size() < 3 ? 1 : args()[2]->evaluate(variables).as_int();
	ASSERT_LOG(step > 0, "ILLEGAL STEP VALUE IN RANGE: " << step);
	bool reverse = false;
	if(end < start) {
		std::swap(start, end);
		++start;
		++end;
		reverse = true;
	}
	const int nelem = end - start;

	std::vector<variant> v;

	if(nelem > 0) {
		v.reserve(nelem/step);

		for(int n = 0; n < nelem; n += step) {
			v.push_back(variant(start+n));
		}
	}

	if(reverse) {
		std::reverse(v.begin(), v.end());
	}

	return variant(&v);
END_FUNCTION_DEF(range)

FUNCTION_DEF(reverse, 1, 1, "reverse(list): reverses the given list")
	std::vector<variant> items = args()[0]->evaluate(variables).as_list();
	std::reverse(items.begin(), items.end());
	return variant(&items);
END_FUNCTION_DEF(reverse)

FUNCTION_DEF(head, 1, 1, "head(list): gives the first element of a list, or null for an empty list")
	const variant items = args()[0]->evaluate(variables);
	if(items.num_elements() >= 1) {
		return items[0];
	} else {
		return variant();
	}
END_FUNCTION_DEF(head)

FUNCTION_DEF(back, 1, 1, "back(list): gives the last element of a list, or null for an empty list")
	const variant items = args()[0]->evaluate(variables);
	if(items.num_elements() >= 1) {
		return items[items.num_elements()-1];
	} else {
		return variant();
	}
END_FUNCTION_DEF(back)

FUNCTION_DEF(get_all_files_under_dir, 1, 1, "get_all_files_under_dir(path): Returns a list of all the files in and under the given directory")
	std::vector<variant> v;
	std::map<std::string, std::string> file_paths;
	module::get_unique_filenames_under_dir(args()[0]->evaluate(variables).as_string(), &file_paths);
	for(std::map<std::string, std::string>::const_iterator i = file_paths.begin(); i != file_paths.end(); ++i) {
		//std::cerr << "FILE " << i->first << " : " << i->second << std::endl;
		v.push_back(variant(i->second));
	}
	return variant(&v);
END_FUNCTION_DEF(get_all_files_under_dir)

FUNCTION_DEF(get_files_in_dir, 1, 1, "get_files_in_dir(path): Returns a list of the files in the given directory")
	std::vector<variant> v;
	std::vector<std::string> files;
	std::string dirname = args()[0]->evaluate(variables).as_string();
	if(dirname[dirname.size()-1] != '/') {
		dirname += '/';
	}
	module::get_files_in_dir(dirname, &files);
	for(std::vector<std::string>::const_iterator i = files.begin(); i != files.end(); ++i) {
		//std::cerr << "FILE " << *i << std::endl;
		v.push_back(variant(*i));
	}
	return variant(&v);
END_FUNCTION_DEF(get_files_in_dir)

FUNCTION_DEF(dialog, 2, 2, "dialog(obj, template): Creates a dialog given an object to operate on and a template for the dialog.")
	bool modal = args().size() == 3 && args()[2]->evaluate(variables).as_bool(); 
	variant environment = args()[0]->evaluate(variables);
	variant dlg_template = args()[1]->evaluate(variables);
	formula_callable* e = environment.try_convert<formula_callable>();
	variant v;
	if(dlg_template.is_string()) {
		std::string s = dlg_template.as_string();
		if(s.length() <= 4 || s.substr(s.length()-4) != ".cfg") {
			s += ".cfg";
		}
		v = json::parse_from_file(gui::get_dialog_file(s));
	} else {
		v = dlg_template;
	}
	return variant(new gui::dialog(v, e));
END_FUNCTION_DEF(dialog)

FUNCTION_DEF(show_modal, 1, 1, "show_modal(dialog): Displays a modal dialog on the screen.")
	variant graph = args()[0]->evaluate(variables);
	gui::dialog_ptr dialog = boost::intrusive_ptr<gui::dialog>(graph.try_convert<gui::dialog>());
	ASSERT_LOG(dialog, "Dialog given is not of the correct type.");
	dialog->show_modal();
	return variant::from_bool(dialog->cancelled() == false);
END_FUNCTION_DEF(show_modal)

FUNCTION_DEF(index, 2, 2, "index(list, value) -> index of value in list: Returns the index of the value in the list or -1 if value wasn't found in the list.")
	variant value = args()[1]->evaluate(variables);
	variant li = args()[0]->evaluate(variables);
	for(int n = 0; n < li.num_elements(); n++) {
		if(value == li[n]) {
			return variant(n);
		}
	}
	return variant(-1);
END_FUNCTION_DEF(index)

namespace {
void evaluate_expr_for_benchmark(const formula_expression* expr, const formula_callable* variables, int ntimes)
{
	for(int n = 0; n < ntimes; ++n) {
		expr->evaluate(*variables);
	}
}

}

FUNCTION_DEF(benchmark, 1, 1, "benchmark(expr): Executes expr in a benchmark harness and returns a string describing its benchmark performance")
	return variant(test::run_benchmark("benchmark", boost::bind(evaluate_expr_for_benchmark, args()[0].get(), &variables, _1)));
END_FUNCTION_DEF(benchmark)

FUNCTION_DEF(compress, 1, 2, "compress(string, (optional) compression_level): Compress the given string object")
	int compression_level = -1;
	if(args().size() > 1) {
		compression_level = args()[1]->evaluate(variables).as_int();
	}
	const std::string s = args()[0]->evaluate(variables).as_string();
	return variant(new zip::compressed_data(std::vector<char>(s.begin(), s.end()), compression_level));
END_FUNCTION_DEF(compress)

FUNCTION_DEF(decompress, 1, 1, "decompress(expr): Tries to decompress the given object, returns the data if successful.")
	variant compressed = args()[0]->evaluate(variables);
	zip::compressed_data_ptr cd = boost::intrusive_ptr<zip::compressed_data>(compressed.try_convert<zip::compressed_data>());
	return cd->get_value("decompress");
END_FUNCTION_DEF(decompress)

	class size_function : public function_expression {
	public:
		explicit size_function(const args_list& args)
			: function_expression("size", args, 1, 1)
		{}
	private:
		variant execute(const formula_callable& variables) const {
			const variant items = args()[0]->evaluate(variables);
			if(items.is_string()) {
				return variant(items.as_string().size());
			}
			return variant(static_cast<int>(items.num_elements()));
		}
	};

	
	class split_function : public function_expression {
	public:
		explicit split_function(const args_list& args)
		: function_expression("split", args, 1, 2)
		{}
	private:
		variant execute(const formula_callable& variables) const {
			std::vector<std::string> chopped;
			if(args().size() >= 2) {
				const std::string thestring = args()[0]->evaluate(variables).as_string();
				const std::string delimiter = args()[1]->evaluate(variables).as_string();
				chopped = util::split(thestring, delimiter);
			} else {
				const std::string thestring = args()[0]->evaluate(variables).as_string();
				chopped = util::split(thestring);
			}
		
			std::vector<variant> res;
			for(size_t i=0; i<chopped.size(); ++i) {
				const std::string& part = chopped[i];
				res.push_back(variant(part));
			}
			
			return variant(&res);
			
		}
	};
	
	class slice_function : public function_expression {
	public:
		explicit slice_function(const args_list& args)
			: function_expression("slice", args, 3, 3)
		{}
	private:
		variant execute(const formula_callable& variables) const {
			const variant list = args()[0]->evaluate(variables);
			if(list.num_elements() == 0) {
				return variant();
			}
			int begin_index = args()[1]->evaluate(variables).as_int()%(list.num_elements()+1);
			int end_index = args()[2]->evaluate(variables).as_int()%(list.num_elements()+1);
			if(end_index >= begin_index) {
				std::vector<variant> result;
				result.reserve(end_index - begin_index);
				while(begin_index != end_index) {
					result.push_back(list[begin_index++]);
				}

				return variant(&result);
			} else {
				return variant();
			}
		}
	};

	class str_function : public function_expression {
	public:
		explicit str_function(const args_list& args)
			: function_expression("str", args, 1, 1)
		{}
	private:
		variant execute(const formula_callable& variables) const {
			const variant item = args()[0]->evaluate(variables);
			if(item.is_string()) {
				//just return as-is for something that's already a string.
				return item;
			}

			std::string str;
			item.serialize_to_string(str);
			return variant(str);
		}
	};

	class strstr_function : public function_expression {
	public:
		explicit strstr_function(const args_list& args)
		: function_expression("strstr", args, 2, 2)
		{}
	private:
		variant execute(const formula_callable& variables) const {
			const variant haystack = args()[0]->evaluate(variables);
			const variant needle = args()[1]->evaluate(variables);
			return variant(strstr(haystack.as_string().c_str(), needle.as_string().c_str()) != NULL);
		}
	};

	class null_function : public function_expression {
	public:
		explicit null_function(const args_list& args)
			: function_expression("null", args, 0, 0)
		{}
	private:
		variant execute(const formula_callable& /*variables*/) const {
			return variant();
		}
	};

	class refcount_function : public function_expression {
	public:
		explicit refcount_function(const args_list& args)
			: function_expression("refcount", args, 1, 1)
		{}
	private:
		variant execute(const formula_callable& variables) const {
			return variant(args()[0]->evaluate(variables).refcount());
		}
	};

	class deserialize_function : public function_expression {
	public:
		explicit deserialize_function(const args_list& args)
		: function_expression("deserialize", args, 1, 1)
		{}

	private:
		variant execute(const formula_callable& variables) const {
			formula::fail_if_static_context();
			const intptr_t id = strtoll(args()[0]->evaluate(variables).as_string().c_str(), NULL, 16);
			return variant::create_variant_under_construction(id);
		}
	};

	class is_string_function : public function_expression {
	public:
		explicit is_string_function(const args_list& args)
			: function_expression("is_string", args, 1, 1)
		{}

	private:
		variant execute(const formula_callable& variables) const {
			return variant(args()[0]->evaluate(variables).is_string());
		}
	};

	class is_null_function : public function_expression {
	public:
		explicit is_null_function(const args_list& args)
			: function_expression("is_null", args, 1, 1)
		{}

	private:
		variant execute(const formula_callable& variables) const {
			return variant(args()[0]->evaluate(variables).is_null());
		}
	};

	class is_int_function : public function_expression {
	public:
		explicit is_int_function(const args_list& args)
			: function_expression("is_int", args, 1, 1)
		{}

	private:
		variant execute(const formula_callable& variables) const {
			return variant(args()[0]->evaluate(variables).is_int());
		}
	};

	class is_decimal_function : public function_expression {
	public:
		explicit is_decimal_function(const args_list& args)
			: function_expression("is_decimal", args, 1, 1)
		{}

	private:
		variant execute(const formula_callable& variables) const {
			return variant(args()[0]->evaluate(variables).is_decimal());
		}
	};

	class is_map_function : public function_expression {
	public:
		explicit is_map_function(const args_list& args)
			: function_expression("is_map", args, 1, 1)
		{}

	private:
		variant execute(const formula_callable& variables) const {
			return variant(args()[0]->evaluate(variables).is_map());
		}
	};

	class mod_function : public function_expression {
		//the standard C++ mod expression does not give correct answers for negative operands - it's "implementation-defined", which means it's not really a modulo operation the way math normally describes them.  To get the right answer, we're using the following - based on the fact that x%y is always in the range [-y+1, y-1], and thus adding y to it is both always enough to make it positive, but doesn't change the modulo value.
	public:
		explicit mod_function(const args_list& args)
		: function_expression("mod", args, 2, 2)
		{}
		
	private:
		variant execute(const formula_callable& variables) const {
			int left = args()[0]->evaluate(variables).as_int();
			int right = args()[1]->evaluate(variables).as_int();
			
			return variant((left%right + right)%right);
		}
	};
	
	class is_function_function : public function_expression {
	public:
		explicit is_function_function(const args_list& args)
			: function_expression("is_function", args, 1, 1)
		{}

	private:
		variant execute(const formula_callable& variables) const {
			return variant(args()[0]->evaluate(variables).is_function());
		}
	};

	class is_list_function : public function_expression {
	public:
		explicit is_list_function(const args_list& args)
			: function_expression("is_list", args, 1, 1)
		{}

	private:
		variant execute(const formula_callable& variables) const {
			return variant(args()[0]->evaluate(variables).is_list());
		}
	};

	class is_callable_function : public function_expression {
	public:
		explicit is_callable_function(const args_list& args)
			: function_expression("is_callable", args, 1, 1)
		{}

	private:
		variant execute(const formula_callable& variables) const {
			return variant(args()[0]->evaluate(variables).is_callable());
		}
	};

	class list_str_function : public function_expression {
	public:
		explicit list_str_function(const args_list& args)
			: function_expression("list_str", args, 1, 1)
		{}

	private:
		variant execute(const formula_callable& variables) const {
			const std::string str = args()[0]->evaluate(variables).as_string();
			std::vector<variant> result;
			
			int count = 0;
			while (str[count] != 0) {
				std::string chr(1,str[count]);
				result.push_back(variant(chr));
				count++;
			}
			return variant(&result);
		}
	};

class set_command : public game_logic::command_callable
{
public:
	set_command(variant target, const std::string& attr, variant val)
	  : target_(target), attr_(attr), val_(val)
	{}
	virtual void execute(game_logic::formula_callable& ob) const {
		if(target_.is_callable()) {
			target_.mutable_callable()->mutate_value(attr_, val_);
		} else if(target_.is_map()) {
			target_.add_attr_mutation(variant(attr_), val_);
		} else {
			ob.mutate_value(attr_, val_);
		}
	}
private:
	mutable variant target_;
	std::string attr_;
	variant val_;
};

class add_command : public game_logic::command_callable
{
public:
	add_command(variant target, const std::string& attr, variant val)
	  : target_(target), attr_(attr), val_(val)
	{}
	virtual void execute(game_logic::formula_callable& ob) const {
		if(target_.is_callable()) {
			target_.mutable_callable()->mutate_value(attr_, target_.mutable_callable()->query_value(attr_) + val_);
		} else if(target_.is_map()) {
			variant key(attr_);
			target_.add_attr_mutation(key, target_[key] + val_);
		} else {
			ob.mutate_value(attr_, ob.query_value(attr_) + val_);
		}
	}
private:
	mutable variant target_;
	std::string attr_;
	variant val_;
};

class set_by_slot_command : public game_logic::command_callable
{
public:
	set_by_slot_command(int slot, const variant& value)
	  : slot_(slot), value_(value)
	{}

	virtual void execute(game_logic::formula_callable& obj) const {
		obj.mutate_value_by_slot(slot_, value_);
	}

	void set_value(const variant& value) { value_ = value; }

private:
	int slot_;
	variant value_;
};

class add_by_slot_command : public game_logic::command_callable
{
public:
	add_by_slot_command(int slot, const variant& value)
	  : slot_(slot), value_(value)
	{}

	virtual void execute(game_logic::formula_callable& obj) const {
		obj.mutate_value_by_slot(slot_, obj.query_value_by_slot(slot_) + value_);
	}

	void set_value(const variant& value) { value_ = value; }

private:
	int slot_;
	variant value_;
};

class set_function : public function_expression {
public:
	set_function(const args_list& args, const formula_callable_definition* callable_def)
	  : function_expression("set", args, 2, 3), slot_(-1) {
		if(args.size() == 2) {
			variant literal = args[0]->is_literal();
			if(literal.is_string()) {
				key_ = literal.as_string();
			} else {
				args[0]->is_identifier(&key_);
			}

			if(!key_.empty() && callable_def) {
				slot_ = callable_def->get_slot(key_);
				if(slot_ != -1) {
					cmd_ = boost::intrusive_ptr<set_by_slot_command>(new set_by_slot_command(slot_, variant()));
				}
			}
		}
	}
private:
	variant execute(const formula_callable& variables) const {
		if(slot_ != -1) {
			if(cmd_->refcount() == 1) {
				cmd_->set_value(args()[1]->evaluate(variables));
				return variant(cmd_.get());
			}

			cmd_ = boost::intrusive_ptr<set_by_slot_command>(new set_by_slot_command(slot_, args()[1]->evaluate(variables)));
			return variant(cmd_.get());
		}

		if(!key_.empty()) {
			return variant(new set_command(variant(), key_, args()[1]->evaluate(variables)));
		}

		if(args().size() == 2) {
			std::string member;
			variant target = args()[0]->evaluate_with_member(variables, member);
			return variant(new set_command(
			  target, member, args()[1]->evaluate(variables)));
		}

		variant target;
		if(args().size() == 3) {
			target = args()[0]->evaluate(variables);
		}
		const int begin_index = args().size() == 2 ? 0 : 1;
		return variant(new set_command(
		    target,
		    args()[begin_index]->evaluate(variables).as_string(),
			args()[begin_index + 1]->evaluate(variables)));
	}

	std::string key_;
	int slot_;
	mutable boost::intrusive_ptr<set_by_slot_command> cmd_;
};

class add_function : public function_expression {
public:
	add_function(const args_list& args, const formula_callable_definition* callable_def)
	  : function_expression("add", args, 2, 3), slot_(-1) {
		if(args.size() == 2) {
			variant literal = args[0]->is_literal();
			if(literal.is_string()) {
				key_ = literal.as_string();
			} else {
				args[0]->is_identifier(&key_);
			}

			if(!key_.empty() && callable_def) {
				slot_ = callable_def->get_slot(key_);
				if(slot_ != -1) {
					cmd_ = boost::intrusive_ptr<add_by_slot_command>(new add_by_slot_command(slot_, variant()));
				}
			}
		}
	}
private:
	variant execute(const formula_callable& variables) const {
		if(slot_ != -1) {
			if(cmd_->refcount() == 1) {
				cmd_->set_value(args()[1]->evaluate(variables));
				return variant(cmd_.get());
			}

			cmd_ = boost::intrusive_ptr<add_by_slot_command>(new add_by_slot_command(slot_, args()[1]->evaluate(variables)));
			return variant(cmd_.get());
		}

		if(!key_.empty()) {
			return variant(new add_command(variant(), key_, args()[1]->evaluate(variables)));
		}

		if(args().size() == 2) {
			std::string member;
			variant target = args()[0]->evaluate_with_member(variables, member);
			return variant(new add_command(
				  target, member, args()[1]->evaluate(variables)));
		}

		variant target;
		if(args().size() == 3) {
			target = args()[0]->evaluate(variables);
		}
		const int begin_index = args().size() == 2 ? 0 : 1;
		return variant(new add_command(
		    target,
		    args()[begin_index]->evaluate(variables).as_string(),
			args()[begin_index + 1]->evaluate(variables)));
	}

	std::string key_;
	int slot_;
	mutable boost::intrusive_ptr<add_by_slot_command> cmd_;
};


class debug_command : public game_logic::command_callable
{
public:
	explicit debug_command(const std::string& str) : str_(str)
	{}
	virtual void execute(formula_callable& ob) const {
#ifndef NO_EDITOR
		debug_console::add_message(str_);
#endif
		std::cerr << "CONSOLE: " << str_ << "\n";
	}
private:
	std::string str_;
};

FUNCTION_DEF(debug, 1, -1, "debug(...): outputs arguments to the console")
	if(!preferences::debug()) {
		return variant();
	}

	std::string str;
	for(int n = 0; n != args().size(); ++n) {
		if(n > 0) {
			str += " ";
		}

		str += args()[n]->evaluate(variables).to_debug_string();
	}

	//fprintf(stderr, "DEBUG FUNCTION: %s\n", str.c_str());

	return variant(new debug_command(str));
END_FUNCTION_DEF(debug)

namespace {
void debug_side_effect(variant v)
{
	if(v.is_list()) {
		foreach(variant item, v.as_list()) {
			debug_side_effect(item);
		}
	} else if(v.is_callable() && v.try_convert<game_logic::command_callable>()) {
		map_formula_callable_ptr obj(new map_formula_callable);
		v.try_convert<game_logic::command_callable>()->execute(*obj);
	} else {
		std::string s = v.to_debug_string();
#ifndef NO_EDITOR
		debug_console::add_message(s);
#endif
		std::cerr << "CONSOLE: " << s << "\n";
	}
}
}

FUNCTION_DEF(debug_fn, 2, 2, "debug_fn(msg, expr): evaluates and returns expr. Will print 'msg' to stderr if it's printable, or execute it if it's an executable command.")
	variant res = args()[1]->evaluate(variables);
	if(preferences::debug()) {
		debug_side_effect(args()[0]->evaluate(variables));
	}

	return res;
END_FUNCTION_DEF(debug_fn)

namespace {
bool consecutive_periods(char a, char b) {
	return a == '.' && b == '.';
}
}

FUNCTION_DEF(get_document, 1, 1, "get_document(string filename): return reference to the given JSON document")
	const std::string docname = args()[0]->evaluate(variables).as_string();

	static std::map<std::string, variant> cache;
	variant& v = cache[docname];
	if(v.is_null() == false) {
		return v;
	}

	ASSERT_LOG(docname.empty() == false, "DOCUMENT NAME GIVEN TO get_document() IS EMPTY");
	ASSERT_LOG(docname[0] != '/', "DOCUMENT NAME BEGINS WITH / " << docname);
	ASSERT_LOG(std::adjacent_find(docname.begin(), docname.end(), consecutive_periods) == docname.end(), "DOCUMENT NAME CONTAINS ADJACENT PERIODS " << docname);

	try {
		const variant v = json::parse_from_file(docname);
		return v;
	} catch(json::parse_error&) {
		return variant();
	}
END_FUNCTION_DEF(get_document)

}

formula_function_expression::formula_function_expression(const std::string& name, const args_list& args, const_formula_ptr formula, const_formula_ptr precondition, const std::vector<std::string>& arg_names)
: function_expression(name, args, arg_names.size(), arg_names.size()),
	formula_(formula), precondition_(precondition), arg_names_(arg_names), star_arg_(-1), has_closure_(false), base_slot_(0)
{
	assert(!precondition_ || !precondition_->str().empty());
	for(size_t n = 0; n != arg_names_.size(); ++n) {
		if(arg_names_.empty() == false && arg_names_[n][arg_names_[n].size()-1] == '*') {
			arg_names_[n].resize(arg_names_[n].size()-1);
			star_arg_ = n;
			break;
		}
	}
}

namespace {
std::stack<const formula_function_expression*> formula_fn_stack;
struct formula_function_scope {
	explicit formula_function_scope(const formula_function_expression* f) {
		formula_fn_stack.push(f);
	}

	~formula_function_scope() {
		formula_fn_stack.pop();
	}
};

bool is_calculating_recursion = false;
struct recursion_calculation_scope {
	recursion_calculation_scope() { is_calculating_recursion = true; }
	~recursion_calculation_scope() { is_calculating_recursion = false; }
};


}

boost::intrusive_ptr<slot_formula_callable> formula_function_expression::calculate_args_callable(const formula_callable& variables) const
{
	if(!callable_ || callable_->refcount() != 1) {
		callable_ = boost::intrusive_ptr<slot_formula_callable>(new slot_formula_callable);
		callable_->reserve(arg_names_.size());
		callable_->set_base_slot(base_slot_);
	}

	callable_->set_names(&arg_names_);

	//we reset callable_ to NULL during any calls so that recursive calls
	//will work properly.
	boost::intrusive_ptr<slot_formula_callable> tmp_callable(callable_);
	callable_.reset(NULL);

	for(int n = 0; n != arg_names_.size(); ++n) {
		variant var = args()[n]->evaluate(variables);
		tmp_callable->add(var);
		if(n == star_arg_) {
			tmp_callable->set_fallback(var.as_callable());
		}
	}

	return tmp_callable;
}

variant formula_function_expression::execute(const formula_callable& variables) const
{
	if(fed_result_) {
		variant result = *fed_result_;
		fed_result_.reset();
		return result;
	}

	boost::intrusive_ptr<slot_formula_callable> tmp_callable = calculate_args_callable(variables);

	if(precondition_) {
		if(!precondition_->execute(*tmp_callable).as_bool()) {
			std::cerr << "FAILED function precondition (" << precondition_->str() << ") for function '" << formula_->str() << "' with arguments: ";
			for(size_t n = 0; n != arg_names_.size(); ++n) {
				std::cerr << "  arg " << (n+1) << ": " << args()[n]->evaluate(variables).to_debug_string() << "\n";
			}
		}
	}

	if(!is_calculating_recursion && formula_->has_guards() && !formula_fn_stack.empty() && formula_fn_stack.top() == this) {
		const recursion_calculation_scope recursion_scope;

		typedef boost::intrusive_ptr<formula_callable> call_ptr;
		std::vector<call_ptr> invocations;
		invocations.push_back(tmp_callable);
		while(formula_->guard_matches(*invocations.back()) == -1) {
			invocations.push_back(calculate_args_callable(*formula_->wrap_callable_with_global_where(*invocations.back())));
		}

		invocations.pop_back();

		if(invocations.size() > 2) {
			while(invocations.empty() == false) {
				fed_result_.reset(new variant(formula_->expr()->evaluate(*formula_->wrap_callable_with_global_where(*invocations.back()))));
				invocations.pop_back();
			}

			variant result = *fed_result_;
			fed_result_.reset();
			return result;
		}
	}

	formula_function_scope scope(this);
	variant res = formula_->execute(*tmp_callable);

	callable_ = tmp_callable;
	callable_->clear();

	return res;
}

	formula_function_expression_ptr formula_function::generate_function_expression(const std::vector<expression_ptr>& args_input) const
	{
		std::vector<expression_ptr> args = args_input;
		if(args.size() + default_args_.size() >= args_.size()) {
			const int base = args_.size() - default_args_.size();
			while(args.size() < args_.size()) {
				const int index = args.size() - base;
				ASSERT_LOG(index >= 0 && index < default_args_.size(), "INVALID INDEX INTO DEFAULT ARGS: " << index << " / " << default_args_.size());
				args.push_back(expression_ptr(new variant_expression(default_args_[index])));
			}
		}

		return formula_function_expression_ptr(new formula_function_expression(name_, args, formula_, precondition_, args_));
	}

	void function_symbol_table::add_formula_function(const std::string& name, const_formula_ptr formula, const_formula_ptr precondition, const std::vector<std::string>& args, const std::vector<variant>& default_args)
	{
		custom_formulas_[name] = formula_function(name, formula, precondition, args, default_args);
	}

	expression_ptr function_symbol_table::create_function(const std::string& fn, const std::vector<expression_ptr>& args, const formula_callable_definition* callable_def) const
	{

		const std::map<std::string, formula_function>::const_iterator i = custom_formulas_.find(fn);
		if(i != custom_formulas_.end()) {
			return i->second.generate_function_expression(args);
		}

		if(backup_) {
			return backup_->create_function(fn, args, callable_def);
		}

		return expression_ptr();
	}

	std::vector<std::string> function_symbol_table::get_function_names() const
	{
		std::vector<std::string> res;
		for(std::map<std::string, formula_function>::const_iterator iter = custom_formulas_.begin(); iter != custom_formulas_.end(); iter++ ) {
			res.push_back((*iter).first);
		}
		return res;
	}

	const formula_function* function_symbol_table::get_formula_function(const std::string& fn) const
	{
		const std::map<std::string, formula_function>::const_iterator i = custom_formulas_.find(fn);
		if(i == custom_formulas_.end()) {
			return NULL;
		} else {
			return &i->second;
		}
	}

	recursive_function_symbol_table::recursive_function_symbol_table(const std::string& fn, const std::vector<std::string>& args, const std::vector<variant>& default_args, function_symbol_table* backup, const formula_callable_definition* closure_definition)
	: name_(fn), stub_(fn, const_formula_ptr(), const_formula_ptr(), args, default_args), backup_(backup), closure_definition_(closure_definition)
	{
	}

	expression_ptr recursive_function_symbol_table::create_function(
					const std::string& fn,
					const std::vector<expression_ptr>& args,
					const formula_callable_definition* callable_def) const
	{
		if(fn == name_) {
			formula_function_expression_ptr expr = stub_.generate_function_expression(args);
			if(closure_definition_) {
				expr->set_has_closure(closure_definition_->num_slots());
			}
			expr_.push_back(expr);
			return expr;
		} else if(backup_) {
			return backup_->create_function(fn, args, callable_def);
		}

		return expression_ptr();
	}

	void recursive_function_symbol_table::resolve_recursive_calls(const_formula_ptr f)
	{
		foreach(formula_function_expression_ptr& fn, expr_) {
			fn->set_formula(f);
		}
	}

namespace {

	typedef std::map<std::string, function_creator*> functions_map;

	functions_map& get_functions_map() {

		static functions_map functions_table;

		if(functions_table.empty()) {
	#define FUNCTION(name) functions_table[#name] = new specific_function_creator<name##_function>();
			FUNCTION(if);
			FUNCTION(filter);
			FUNCTION(mapping);
			FUNCTION(find);
			FUNCTION(visit_objects);
			FUNCTION(map);
			FUNCTION(sum);
			FUNCTION(range);
			FUNCTION(head);
			FUNCTION(size);
			FUNCTION(split);
			FUNCTION(slice);
			FUNCTION(str);
			FUNCTION(strstr);
			FUNCTION(null);
			FUNCTION(refcount);
			FUNCTION(deserialize);
			FUNCTION(is_string);
			FUNCTION(is_null);
			FUNCTION(is_int);
			FUNCTION(is_decimal);
			FUNCTION(is_map);
			FUNCTION(mod);
			FUNCTION(is_function);
			FUNCTION(is_list);
			FUNCTION(is_callable);
			FUNCTION(list_str);
	#undef FUNCTION
		}

		return functions_table;
	}

}

expression_ptr create_function(const std::string& fn,
                               const std::vector<expression_ptr>& args,
							   const function_symbol_table* symbols,
							   const formula_callable_definition* callable_def)
{
	if(fn == "set") {
		return expression_ptr(new set_function(args, callable_def));
	} else if(fn == "add") {
		return expression_ptr(new add_function(args, callable_def));
	}

	if(symbols) {
		expression_ptr res(symbols->create_function(fn, args, callable_def));
		if(res) {
			return res;
		}
	}

	const std::map<std::string, function_creator*>& creators = get_function_creators(FunctionModule);
	std::map<std::string, function_creator*>::const_iterator creator_itor = creators.find(fn);
	if(creator_itor != creators.end()) {
		return expression_ptr(creator_itor->second->create(args));
	}

	functions_map::const_iterator i = get_functions_map().find(fn);
	if(i == get_functions_map().end()) {
		return expression_ptr();
	}

	return expression_ptr(i->second->create(args));
}

std::vector<std::string> builtin_function_names()
{
	std::vector<std::string> res;
	const functions_map& m = get_functions_map();
	for(functions_map::const_iterator i = m.begin(); i != m.end(); ++i) {
		res.push_back(i->first);
	}

	return res;
}

function_expression::function_expression(
                    const std::string& name,
                    const args_list& args,
                    int min_args, int max_args)
    : name_(name), args_(args), min_args_(min_args), max_args_(max_args)
{
	set_name(name.c_str());
}

void function_expression::set_debug_info(const variant& parent_formula,
	                            std::string::const_iterator begin_str,
	                            std::string::const_iterator end_str)
{
	formula_expression::set_debug_info(parent_formula, begin_str, end_str);

	if(min_args_ >= 0 && args_.size() < static_cast<size_t>(min_args_) ||
	   max_args_ >= 0 && args_.size() > static_cast<size_t>(max_args_)) {
		ASSERT_LOG(false, "ERROR: incorrect number of arguments to function '" << name_ << "': expected between " << min_args_ << " and " << max_args_ << ", found " << args_.size() << "\n" << debug_pinpoint_location());
	}
}

namespace {
bool point_in_triangle(point p, point t[3]) 
{
	point v0(t[2].x - t[0].x, t[2].y - t[0].y);
	point v1(t[1].x - t[0].x, t[1].y - t[0].y);
	point v2(p.x - t[0].x, p.y - t[0].y);

	int dot00 = t[0].x * t[0].x + t[0].y * t[0].y;
	int dot01 = t[0].x * t[1].x + t[0].y * t[1].y;
	int dot02 = t[0].x * t[2].x + t[0].y * t[2].y;
	int dot11 = t[1].x * t[1].x + t[1].y * t[1].y;
	int dot12 = t[1].x * t[2].x + t[1].y * t[2].y;
	float invDenom = 1 / float(dot00 * dot11 - dot01 * dot01);
	float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
	float v = (dot00 * dot12 - dot01 * dot02) * invDenom;
	return u >= 0.0f && v >= 0.0f && (u+v) < 1.0f;
}
}

FUNCTION_DEF(hex_get_tile_at, 3, 3, "hex_get_tile_at(hexmap, x, y) -> hex_tile object: Finds the hex tile at the given level co-ordinates")
	// Because we assume hexes are placed at a regular series of intervals
	variant v = args()[0]->evaluate(variables);
	hex::hex_map_ptr hexmap = hex::hex_map_ptr(v.try_convert<hex::hex_map>());
	ASSERT_LOG(hexmap, "hexmap not of the correct type.");
	const int mx = args()[1]->evaluate(variables).as_int();
	const int my = args()[2]->evaluate(variables).as_int();

	return variant(hexmap->get_tile_from_pixel_pos(mx, my).get());
END_FUNCTION_DEF(hex_get_tile_at)

FUNCTION_DEF(pixel_to_tile_coords, 1, 2, "pixel_to_tile_coords(args) -> [x,y]: Gets the tile at the pixel position given in the arguments. The position"
	"can either be a single list of two values suck as [x,y] or two seperate x,y co-ordinates.")
	int x, y;
	if(args().size() == 1) {
		variant vl = args()[0]->evaluate(variables);
		ASSERT_LOG(vl.is_list() && vl.num_elements() == 2, "Single argument must be a list of two elements");
		x = vl[0].as_int();
		y = vl[1].as_int();
	} else {
		x = args()[0]->evaluate(variables).as_int();
		y = args()[1]->evaluate(variables).as_int();
	}
	point xy = hex::hex_map::get_tile_pos_from_pixel_pos(x,y);
	std::vector<variant> v;
	v.push_back(variant(xy.x));
	v.push_back(variant(xy.y));
	return variant(&v);
END_FUNCTION_DEF(pixel_to_tile_coords)

FUNCTION_DEF(tile_to_pixel_coords, 2, 3, "tile_to_pixel_coords(x, y, (opt)string) -> [x,y]: Gets the center pixel co-ordinates of a given tile co-ordinate."
	"string can be effect the co-ordinates returned. \"bounding\" -> [x,y,w,h] Bounding rect of the tile. \"center\" -> [x,y] center co-ordinates of the tile(default)"
	"\"hex\" -> [[x0,y0],[x1,y1],[x2,y2],[x3,y3],[x4,y4],[x5,y5]] Co-ordinates of points around outside of the tile.")
	const int x = args()[0]->evaluate(variables).as_int();
	const int y = args()[1]->evaluate(variables).as_int();
	point p(hex::hex_map::get_pixel_pos_from_tile_pos(x, y));
	std::vector<variant> v;
	const int HexTileSize = 72;
	if(args().size() > 2) {
		const std::string opt(args()[2]->evaluate(variables).as_string());
		if(opt == "bounding" || opt == "rect") {
			v.push_back(variant(p.x));
			v.push_back(variant(p.y));
			v.push_back(variant(HexTileSize));
			v.push_back(variant(HexTileSize));
		} else if(opt == "hex") {
			const float angle = 2.0f * 3.14159265358979f / 6.0f;
			for(int i = 0; i < 6; i++) {
				v.push_back(variant(decimal(p.x + HexTileSize/2 + HexTileSize/2.0f * sin(i * angle))));
				v.push_back(variant(decimal(p.y + HexTileSize/2 + HexTileSize/2.0f * cos(i * angle))));
			}
		} else {
			v.push_back(variant(p.x + HexTileSize/2));
			v.push_back(variant(p.y + HexTileSize/2));
		}
		// unknown just drop down and do default
	} else {
		v.push_back(variant(p.x + HexTileSize/2));
		v.push_back(variant(p.y + HexTileSize/2));
	}
	return variant(&v);
END_FUNCTION_DEF(tile_to_pixel_coords)

FUNCTION_DEF(hex_pixel_coords, 2, 2, "hex_pixel_coords(x,y) -> [x,y]: Converts a pair of pixel co-ordinates to the corresponding tile co-ordinate.")
	const int x = args()[0]->evaluate(variables).as_int();
	const int y = args()[1]->evaluate(variables).as_int();
	point p(hex::hex_map::get_tile_pos_from_pixel_pos(x, y));
	std::vector<variant> v;
	v.push_back(variant(p.x));
	v.push_back(variant(p.y));
	return variant(&v);
END_FUNCTION_DEF(hex_pixel_coords)

FUNCTION_DEF(hex_location, 3, 3, "hex_location(x,y,string dir) -> [x,y]: calculates the co-ordinates of the tile in the given direction.")
	const int x = args()[0]->evaluate(variables).as_int();
	const int y = args()[1]->evaluate(variables).as_int();
	variant d = args()[2]->evaluate(variables);
	point p(x,y);
	if(d.is_list()) {
		for(int i = 0; i < d.num_elements(); i++) {
			p = hex::hex_map::loc_in_dir(p.x, p.y, d[i].as_string());
		}
	} else if(d.is_string()) {
		const std::string dir(d.as_string());
		p = hex::hex_map::loc_in_dir(x, y, dir);
	}
	std::vector<variant> v;
	v.push_back(variant(p.x));
	v.push_back(variant(p.y));
	return variant(&v);
END_FUNCTION_DEF(hex_location)

FUNCTION_DEF(hex_get_tile, 1, 1, "hex_get_tile(string) -> hex_tile object: Returns a hex tile object with the given name.")
	const std::string& tstr(args()[0]->evaluate(variables).as_string());
	return variant(hex::hex_object::get_hex_tile(tstr).get());
END_FUNCTION_DEF(hex_get_tile)

FUNCTION_DEF(hex_get_random_tile, 1, 2, "hex_get_random_tile(regex, (opt)count) -> hex_tile object(s): Generates either a single random tile or an array of count random tiles, picked from the given regular expression")
	const boost::regex re(args()[0]->evaluate(variables).as_string());
	std::vector<hex::hex_tile_ptr>& tile_list = hex::hex_object::get_editor_tiles();
	std::vector<hex::hex_tile_ptr> matches;
	for(size_t i = 0; i < tile_list.size(); ++i) {
		if(boost::regex_match(tile_list[i]->get_editor_info().type, re)) {
			matches.push_back(tile_list[i]);
		}
	}
	if(matches.empty()) {
		return variant();
	}
	if(args().size() > 1) {
		const int count = args()[1]->evaluate(variables).as_int();
		std::vector<variant> v;
		for(int i = 0; i < count; ++i ) {
			v.push_back(variant(matches[rand() % matches.size()].get()));
		}
		return variant(&v);
	} else {
		return variant(matches[rand() % matches.size()].get());
	}
END_FUNCTION_DEF(hex_get_random_tile)

}

UNIT_TEST(modulo_operation) {
	CHECK(game_logic::formula(variant("mod(-5, 20)")).execute() == game_logic::formula(variant("15")).execute(), "test failed");
	CHECK(game_logic::formula(variant("mod(-25, 20)")).execute() == game_logic::formula(variant("15")).execute(), "test failed");
	CHECK(game_logic::formula(variant("mod(15, 20)")).execute() == game_logic::formula(variant("15")).execute(), "test failed");
	CHECK(game_logic::formula(variant("mod(35, 20)")).execute() == game_logic::formula(variant("15")).execute(), "test failed");
}

UNIT_TEST(flatten_function) {
	CHECK(game_logic::formula(variant("flatten([1,[2,3]])")).execute() == game_logic::formula(variant("[1,2,3]")).execute(), "test failed");
	CHECK(game_logic::formula(variant("flatten([1,2,3,[[4,5],6]])")).execute() == game_logic::formula(variant("[1,2,3,4,5,6]")).execute(), "test failed");
	CHECK(game_logic::formula(variant("flatten([[1,2,3,4],5,6])")).execute() == game_logic::formula(variant("[1,2,3,4,5,6]")).execute(), "test failed");
	CHECK(game_logic::formula(variant("flatten([[[0,2,4],6,8],10,[12,14]])")).execute() == game_logic::formula(variant("[0,2,4,6,8,10,12,14]")).execute(), "test failed");
}

UNIT_TEST(sqrt_function) {
	CHECK_EQ(game_logic::formula(variant("sqrt(2147483)")).execute().as_int(), 1465);	

	for(uint64_t n = 0; n < 100000; n += 1000) {
		CHECK_EQ(game_logic::formula(variant(formatter() << "sqrt(" << n << ".0^2)")).execute().as_decimal(), decimal::from_int(n));
	}
}

UNIT_TEST(map_function) {
	CHECK_EQ(game_logic::formula(variant("map([2,3,4], value+index)")).execute(), game_logic::formula(variant("[2,4,6]")).execute());
}

UNIT_TEST(where_scope_function) {
	CHECK(game_logic::formula(variant("{'val': num} where num = 5")).execute() == game_logic::formula(variant("{'val': 5}")).execute(), "map where test failed");
	CHECK(game_logic::formula(variant("'five: ${five}' where five = 5")).execute() == game_logic::formula(variant("'five: 5'")).execute(), "string where test failed");
}

BENCHMARK(map_function) {
	using namespace game_logic;

	static map_formula_callable* callable = NULL;
	static variant callable_var;
	static variant main_callable_var;
	static std::vector<variant> v;
	
	if(callable == NULL) {
		callable = new map_formula_callable;
		callable_var = variant(callable);
		callable->add("x", variant(0));
		for(int n = 0; n != 1000; ++n) {
			v.push_back(callable_var);
		}

		callable = new map_formula_callable;
		main_callable_var = variant(callable);
		callable->add("items", variant(&v));
	}

	static formula f(variant("map(items, 'obj', 0)"));
	BENCHMARK_LOOP {
		f.execute(*callable);
	}
}
