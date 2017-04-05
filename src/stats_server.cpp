#include <map>
#include <vector>

#include "foreach.hpp"
#include "formula.hpp"
#include "formula_callable.hpp"
#include "stats_server.hpp"

namespace {

using namespace game_logic;

class table_info
{
public:
	explicit table_info(const variant& v);
	const std::string& name() const { return name_; }
	bool is_global() const { return is_global_; }

	variant init_value() const { return init_value_; }
	variant calculate_key(const variant& msg, const formula_callable& context_callable) const;
	variant calculate_value(const variant& msg, const variant& current_value) const;
private:
	std::string name_;
	bool is_global_;

	const_formula_ptr key_;
	const_formula_ptr value_;
	variant init_value_;
};

table_info::table_info(const variant& v)
  : name_(v["name"].as_string()),
    is_global_(v["global_scope"].as_bool()),
    key_(formula::create_optional_formula(v["key"])),
	value_(formula::create_optional_formula(v["value"])),
	init_value_(v["init_value"])
{
}

namespace {
class variant_callable : public formula_callable {
	variant var_;
	variant get_value(const std::string& key) const {
		return var_[variant(key)];
	}
public:
	explicit variant_callable(const variant& v) : var_(v)
	{}
};

}

variant table_info::calculate_key(const variant& msg, const formula_callable& context_callable) const
{
	if(key_) {
		variant_callable* v = new variant_callable(msg);
		variant holder(v);
		formula_callable_with_backup* callable = new formula_callable_with_backup(*v, context_callable);
		variant callable_holder(callable);
		return key_->execute(*callable);
	} else {
		return variant();
	}
}

variant table_info::calculate_value(const variant& msg, const variant& current_value) const
{
	if(value_) {
		map_formula_callable* callable = new map_formula_callable;
		const variant scope(callable);
		callable->add("value", current_value);
		callable->add("sample", msg);
		return value_->execute(*callable);
	} else {
		if(current_value.is_int() || current_value.is_null()) {
			return variant(current_value.as_int()+1);
		} else {
			return current_value;
		}
	}
}

struct msg_type_info {
	std::string name;
	std::vector<table_info> tables;
};

std::map<std::string, msg_type_info> message_type_index;

typedef std::map<variant, variant> table;

variant output_table(const table& t) {
	std::vector<variant> v;
	for(table::const_iterator i = t.begin(); i != t.end(); ++i) {
		std::map<variant, variant> m;
		m[variant("key")] = i->first;
		m[variant("value")] = i->second;
		v.push_back(variant(&m));
	}

	return variant(&v);
}

table read_table(const variant& v) {
	table result;
	for(int n = 0; n != v.num_elements(); ++n) {
		result.insert(std::pair<variant, variant>(v[n]["key"], v[n]["value"]));
	}

	return result;
}

struct table_set {
	int total_count;
	std::map<std::string, table> tables;
};

typedef std::map<std::string, table_set> type_data_map;

variant output_type_data_map(const type_data_map& m) {
	std::vector<variant> type_vec;
	for(type_data_map::const_iterator i = m.begin(); i != m.end(); ++i) {
		std::map<variant, variant> obj;
		obj[variant("type")] = variant(i->first);
		obj[variant("total")] = variant(i->second.total_count);

		std::vector<variant> tables;
		for(std::map<std::string, table>::const_iterator j = i->second.tables.begin(); j != i->second.tables.end(); ++j) {
			std::map<variant, variant> table_obj;
			table_obj[variant("name")] = variant(j->first);
			table_obj[variant("entries")] = output_table(j->second);
			tables.push_back(variant(&table_obj));
		}

		obj[variant("tables")] = variant(&tables);

		type_vec.push_back(variant(&obj));
	}

	return variant(&type_vec);
}

type_data_map read_type_data_map(variant v) {
	type_data_map result;
	for(int n = 0; n != v.num_elements(); ++n) {
		const variant& obj = v[n];

		table_set ts;
		ts.total_count = obj["total"].as_int();

		const variant& tables_v = obj["tables"];
		for(int m = 0; m != tables_v.num_elements(); ++m) {
			const std::string table_name = tables_v[m]["name"].as_string();

			ts.tables[table_name] = read_table(tables_v[m]["entries"]);
		}

		result[obj["type"].as_string()] = ts;
	}

	return result;
}

struct version_data {
	type_data_map global_data;
	std::map<std::string, type_data_map> level_to_data;
};

version_data read_version_data(variant v)
{
	version_data result;
	variant keys = v.get_keys();
	for(int n = 0; n != keys.num_elements(); ++n) {
		if(keys[n].as_string() == "_GLOBAL_") {
			result.global_data = read_type_data_map(v[keys[n]]);
		} else {
			result.level_to_data[keys[n].as_string()] = read_type_data_map(v[keys[n]]);
		}
	}

	return result;
}

variant write_version_data(const version_data& d)
{
	std::map<variant, variant> result;
	result[variant("_GLOBAL_")] = output_type_data_map(d.global_data);
	for(std::map<std::string, type_data_map>::const_iterator i = d.level_to_data.begin(); i != d.level_to_data.end(); ++i) {
		result[variant(i->first)] = output_type_data_map(i->second);
	}

	return variant(&result);
}

//keyed by version.
std::map<std::string, version_data> data_table;

variant write_data_table()
{
	std::map<variant, variant> result;
	for(std::map<std::string, version_data>::const_iterator i = data_table.begin(); i != data_table.end(); ++i) {
		result[variant(i->first)] = write_version_data(i->second);
	}

	return variant(&result);
}

void read_data_table(variant v)
{
	data_table.clear();
	variant keys = v.get_keys();
	for(int n = 0; n != keys.num_elements(); ++n) {
		data_table[keys[n].as_string()] = read_version_data(v[keys[n]]);
	}
}

}

void init_tables(const variant& doc)
{
	for(int n = 0; n != doc.num_elements(); ++n) {
		variant v = doc[n];
		msg_type_info& info = message_type_index[v["name"].as_string()];
		info.name = v["name"].as_string();
		variant tables_v = v["tables"];
		for(int m = 0; m != tables_v.num_elements(); ++m) {
			info.tables.push_back(table_info(tables_v[m]));
		}
	}
}

void read_stats(const variant& doc)
{
	read_data_table(doc);
}

variant write_stats()
{
	return write_data_table();
}

void process_stats(const variant& doc)
{
	variant version = doc["version"];
	if(!version.is_string()) {
		return;
	}

	const std::string& version_str = version.as_string();
	const int user_id = doc["user_id"].as_int();

	game_logic::map_formula_callable* context_callable = new game_logic::map_formula_callable;
	context_callable->add("user_id", variant(user_id));
	variant context_holder(context_callable);

	version_data* data_store[2];
	data_store[0] = &data_table[version_str];
	data_store[1] = &data_table[""];

	variant levels = doc["levels"];	
	if(!levels.is_list()) {
		return;
	}

	for(int n = 0; n != levels.num_elements(); ++n) {
		variant lvl = levels[n];
		variant level_id = lvl["level"];
		if(!level_id.is_string()) {
			continue;
		}

		variant stats = lvl["stats"];
		for(int m = 0; m != stats.num_elements(); ++m) {
			variant msg = stats[m];
			if(!msg.is_map()) {
				continue;
			}

			variant type = msg["type"];
			if(!type.is_string()) {
				continue;
			}
			
			const std::string& type_str = type.as_string();
			const msg_type_info& msg_info = message_type_index[type_str];

			table_set* all_ts[4];

			table_set** global_ts = &all_ts[0];
			table_set** level_ts = &all_ts[2];
			for(int i = 0; i != 2; ++i) {
				global_ts[i] = &data_store[i]->global_data[type_str];
				global_ts[i]->total_count++;

				type_data_map& data_map = data_store[i]->level_to_data[level_id.as_string()];
				level_ts[i] = &data_map[type_str];
				level_ts[i]->total_count++;
			}

			foreach(const table_info& info, msg_info.tables) {
				variant key = info.calculate_key(msg, *context_callable);
				for(int i = (info.is_global() ? 0 : 2); i != 4; ++i) {
					table_set* ts = all_ts[i];
					table& tb = ts->tables[info.name()];
					variant& val = tb[key];
					val = info.calculate_value(msg, val);
				}
			}
		}
	}
}

variant get_stats(const std::string& version, const std::string& lvl)
{
	version_data& ver_data = data_table[version];
	type_data_map& data = lvl.empty() ? ver_data.global_data : ver_data.level_to_data[lvl];
	return output_type_data_map(data);
}
