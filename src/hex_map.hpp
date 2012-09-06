#pragma once
#ifndef HEX_MAP_HPP_INCLUDED
#define HEX_MAP_HPP_INCLUDED

#include <boost/intrusive_ptr.hpp>
#include <vector>

namespace hex {
	enum direction {NORTH, SOUTH, NORTH_WEST, NORTH_EAST, SOUTH_WEST, SOUTH_EAST};
}

#include "hex_object_fwd.hpp"
#include "hex_object.hpp"
#include "formula_callable.hpp"
#include "variant.hpp"

namespace hex {

typedef std::vector<hex_object_ptr> hex_tile_row;
typedef std::vector<hex_tile_row> hex_tile_map;

class hex_map
{
public:
	hex_map() : zorder_(-1000), width_(0), height_(0), x_(0), y_(0)
	{}
	virtual ~hex_map()
	{}
	explicit hex_map(variant node);
	int zorder() const { return zorder_; }

	size_t width() const { return width_; }
	size_t height() const { return height_; }
	size_t size() const { return width_ * height_; }
	void build();
	virtual void draw() const;
	variant write() const;

	hex_object_ptr get_hex_tile(enum hex::direction d, int x, int y) const;
private:
	hex_tile_map tiles_;
	size_t width_;
	size_t height_;
	int x_;
	int y_;
	int zorder_;
};

}

#endif
