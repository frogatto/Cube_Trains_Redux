#include <GL/gl.h>
#include <GL/glu.h>
#include <SDL/SDL.h>
#include <iostream>

#include <boost/shared_ptr.hpp>

#include "character.hpp"
#include "character_type.hpp"
#include "draw_tile.hpp"
#include "entity.hpp"
#include "filesystem.hpp"
#include "font.hpp"
#include "foreach.hpp"
#include "formatter.hpp"
#include "frame.hpp"
#include "item.hpp"
#include "key.hpp"
#include "level.hpp"
#include "level_object.hpp"
#include "raster.hpp"
#include "texture.hpp"
#include "tile_map.hpp"
#include "wml_node.hpp"
#include "wml_parser.hpp"
#include "wml_utils.hpp"
#include "wml_writer.hpp"

namespace {

struct tileset {
	static void init(wml::const_node_ptr node);
	explicit tileset(wml::const_node_ptr node);
	std::string type;
	int zorder;
	boost::shared_ptr<tile_map> preview;
	bool sloped;
};

std::vector<tileset> tilesets;
int cur_tileset = 0;

void tileset::init(wml::const_node_ptr node)
{
	wml::node::const_child_iterator i1 = node->begin_child("tileset");
	wml::node::const_child_iterator i2 = node->end_child("tileset");
	for(; i1 != i2; ++i1) {
		tilesets.push_back(tileset(i1->second));
	}
}

tileset::tileset(wml::const_node_ptr node) : type(node->attr("type")), zorder(wml::get_int(node, "zorder")), sloped(wml::get_bool(node, "sloped"))
{
	if(node->get_child("preview")) {
		preview.reset(new tile_map(node->get_child("preview")));
	}
}

struct enemy_type {
	static void init(wml::const_node_ptr node);
	explicit enemy_type(wml::const_node_ptr node);
	wml::const_node_ptr node;
	const frame* preview_frame;
};

std::vector<enemy_type> enemy_types;
int cur_enemy_type = 0;

void enemy_type::init(wml::const_node_ptr node)
{
	wml::node::const_child_iterator i1 = node->begin_child("character");
	wml::node::const_child_iterator i2 = node->end_child("character");
	for(; i1 != i2; ++i1) {
		enemy_types.push_back(enemy_type(i1->second));
	}
}

enemy_type::enemy_type(wml::const_node_ptr node)
  : node(node), preview_frame(&entity::build(node)->current_frame())
{}

struct placeable_item {
	static void init(wml::const_node_ptr node);
	explicit placeable_item(wml::const_node_ptr node);
	wml::const_node_ptr node;
	const_item_type_ptr type;
};

std::vector<placeable_item> placeable_items;
int cur_item = 0;

void placeable_item::init(wml::const_node_ptr node)
{
	wml::node::const_child_iterator i1 = node->begin_child("item");
	wml::node::const_child_iterator i2 = node->end_child("item");
	for(; i1 != i2; ++i1) {
		placeable_items.push_back(placeable_item(i1->second));
	}

	std::cerr << "ITEMS: " << placeable_items.size() << "\n";
}

placeable_item::placeable_item(wml::const_node_ptr node)
  : node(node), type(item_type::get((*node)["type"]))
{}

entity_ptr selected_entity;
int selected_property = 0;

}

void edit_level(const char* level_cfg)
{
	static bool first_time = true;
	if(first_time) {
		wml::const_node_ptr editor_cfg = wml::parse_wml(sys::read_file("editor.cfg"));
		tileset::init(editor_cfg);
		enemy_type::init(editor_cfg);
		placeable_item::init(editor_cfg);
		first_time = false;
	}

	assert(!tilesets.empty());
	level lvl(level_cfg);
	lvl.finish_loading();
	lvl.set_editor();

	glEnable(GL_SMOOTH);
	glEnable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	const int TileSize = 32;

	std::vector<level_tile> tileset_preview;

	int selected_tile = 0;
	bool face_right = true;

	enum EDIT_MODE { EDIT_TILES, EDIT_CHARS, EDIT_ITEMS, EDIT_GROUPS, EDIT_PROPERTIES, EDIT_VARIATIONS };
	EDIT_MODE mode = EDIT_TILES; 

	CKey key;
	bool done = false;
	int xpos = 0, ypos = 0;
	int zorder = 0;
	int anchorx = 0, anchory = 0;
	while(!done) {
		if(tileset_preview.empty()) {
			assert(cur_tileset < tilesets.size());
			if(tilesets[cur_tileset].preview) {
				tilesets[cur_tileset].preview->build_tiles(&tileset_preview);
			}
		}

		int mousex, mousey;
		const unsigned int buttons = SDL_GetMouseState(&mousex, &mousey);

		const int selectx = (xpos + mousex)/TileSize;
		const int selecty = (ypos + mousey)/TileSize;

		const int ScrollSpeed = 8;

		const bool ctrl = key[SDLK_LCTRL] || key[SDLK_RCTRL];

		if(!ctrl) {
			if(key[SDLK_LEFT]) {
				xpos -= ScrollSpeed;
			}

			if(key[SDLK_RIGHT]) {
				xpos += ScrollSpeed;
			}

			if(key[SDLK_UP]) {
				ypos -= ScrollSpeed;
			}

			if(key[SDLK_DOWN]) {
				ypos += ScrollSpeed;
			}
		} else {
			const rect& bounds = lvl.boundaries();
			if(key[SDLK_LEFT]) {
				if(key[SDLK_LEFT] && bounds.w() > TileSize) {
					lvl.set_boundaries(rect(bounds.x(), bounds.y(), bounds.w() - TileSize, bounds.h()));
				}
			}

			if(key[SDLK_RIGHT]) {
				lvl.set_boundaries(rect(bounds.x(), bounds.y(), bounds.w() + TileSize, bounds.h()));
			}

			if(key[SDLK_UP] && bounds.h() > TileSize) {
				lvl.set_boundaries(rect(bounds.x(), bounds.y(), bounds.w(), bounds.h() - TileSize));
			}

			if(key[SDLK_DOWN]) {
				lvl.set_boundaries(rect(bounds.x(), bounds.y(), bounds.w(), bounds.h() + TileSize));
			}
		}

		SDL_Event event;
		while(SDL_PollEvent(&event)) {
			switch(event.type) {
			case SDL_QUIT:
				done = true;
				break;
			case SDL_KEYDOWN:
				std::cerr << "keydown " << (int)event.key.keysym.sym << " vs " << (int)SDLK_LEFT << "\n";
				if(event.key.keysym.sym == SDLK_ESCAPE) {
					return;
				}

				if(mode == EDIT_PROPERTIES &&
				   selected_entity &&
				   event.key.keysym.sym >= SDLK_1 &&
				   event.key.keysym.sym <= SDLK_9) {
					const int num = event.key.keysym.sym - SDLK_1;
					game_logic::formula_callable* vars = selected_entity->vars();
					if(vars) {
						std::vector<game_logic::formula_input> inputs;
						vars->get_inputs(&inputs);
						if(num < inputs.size()) {
							selected_property = num;
						}
					}
				}

				if(mode == EDIT_PROPERTIES && selected_entity &&
				   (event.key.keysym.sym == SDLK_COMMA ||
				    event.key.keysym.sym == SDLK_PERIOD)) { 
					int increment = (event.key.keysym.sym == SDLK_COMMA ? -1 : 1) * ((event.key.keysym.mod & KMOD_SHIFT) ? 10 : 1);

					game_logic::formula_callable* vars = selected_entity->vars();
					if(vars) {
						std::vector<game_logic::formula_input> inputs;
						vars->get_inputs(&inputs);
						if(selected_property < inputs.size()) {
							const std::string& key = inputs[selected_property].name;
							vars->mutate_value(key, vars->query_value(key) + variant(increment));
						}
					}
				}

				if(event.key.keysym.sym == SDLK_s) {
					std::string data;
					wml::write(lvl.write(), data);
					sys::write_file(level_cfg, data);
				}

				if(event.key.keysym.sym == SDLK_f) {
					face_right = !face_right;
				}

				if(event.key.keysym.sym == SDLK_c &&
				   enemy_types.empty() == false) {
					mode = EDIT_CHARS;
				}

				if(event.key.keysym.sym == SDLK_t) {
					mode = EDIT_TILES;
				}

				if(event.key.keysym.sym == SDLK_v) {
					mode = EDIT_VARIATIONS;
				}

				if(event.key.keysym.sym == SDLK_i &&
				   placeable_items.empty() == false) {
					mode = EDIT_ITEMS;
				}

				if(event.key.keysym.sym == SDLK_g) {
					mode = EDIT_GROUPS;
				}

				if(event.key.keysym.sym == SDLK_p) {
					mode = EDIT_PROPERTIES;
				}

				if(event.key.keysym.sym == SDLK_r &&
				   (event.key.keysym.mod&KMOD_CTRL)) {
					tile_map::init(wml::parse_wml(sys::read_file("tiles.cfg")));
					lvl.rebuild_tiles();
				}

				if(event.key.keysym.sym == SDLK_COMMA) {
					switch(mode) {
					case EDIT_TILES:
						--cur_tileset;
						if(cur_tileset < 0) {
							cur_tileset = tilesets.size()-1;
						}
						tileset_preview.clear();
						break;
					case EDIT_CHARS:
						--cur_enemy_type;
						if(cur_enemy_type < 0) {
							cur_enemy_type = enemy_types.size()-1;
						}
						break;
					case EDIT_ITEMS:
						--cur_item;
						if(cur_item < 0) {
							cur_item = placeable_items.size()-1;
						}
					}
				}

				if(event.key.keysym.sym == SDLK_PERIOD) {
					switch(mode) {
					case EDIT_TILES:
						++cur_tileset;
						if(cur_tileset == tilesets.size()) {
							cur_tileset = 0;
						}

						tileset_preview.clear();
						break;
					case EDIT_CHARS:
						++cur_enemy_type;
						if(cur_enemy_type == enemy_types.size()) {
							cur_enemy_type = 0;
						}
						break;
					case EDIT_ITEMS:
						++cur_item;
						if(cur_item == placeable_items.size()) {
							cur_item = 0;
						}
						break;
					}
				}

				break;
			case SDL_MOUSEBUTTONDOWN:
				anchorx = xpos + mousex;
				anchory = ypos + mousey;

				if(mode == EDIT_CHARS && event.button.button == SDL_BUTTON_LEFT) {
					wml::node_ptr node(wml::deep_copy(enemy_types[cur_enemy_type].node));
					node->set_attr("x", formatter() << (anchorx - anchorx%TileSize));
					node->set_attr("y", formatter() << (anchory - anchory%TileSize));
					node->set_attr("face_right", face_right ? "yes" : "no");

					wml::node_ptr vars = node->get_child("vars");
					entity_ptr c(entity::build(node));
					if(vars) {
						std::map<std::string, std::string> attr;
						for(wml::node::const_attr_iterator i = vars->begin_attr();
						    i != vars->end_attr(); ++i) {
							game_logic::formula_ptr f = game_logic::formula::create_string_formula(i->second);
							if(f) {
								attr[i->first] = f->execute(*c).as_string();
							}
						}

						for(std::map<std::string, std::string>::const_iterator i = attr.begin(); i != attr.end(); ++i) {
							vars->set_attr(i->first, i->second);
						}

						c = entity::build(node);
					}
					lvl.add_character(c);
				} else if(mode == EDIT_ITEMS && event.button.button == SDL_BUTTON_LEFT) {
					wml::node_ptr node(wml::deep_copy(placeable_items[cur_item].node));
					node->set_attr("x", formatter() << (anchorx - anchorx%TileSize));
					node->set_attr("y", formatter() << (anchory - anchory%TileSize));
					item_ptr i(new item(node));
					lvl.add_item(i);
				}
				break;
			case SDL_MOUSEBUTTONUP:
				if(mode == EDIT_TILES) {
					if(event.button.button == SDL_BUTTON_LEFT) {
						lvl.add_tile_rect(tilesets[cur_tileset].zorder, anchorx, anchory, xpos + mousex, ypos + mousey, tilesets[cur_tileset].type);
					} else if(event.button.button == SDL_BUTTON_RIGHT) {
						lvl.clear_tile_rect(anchorx, anchory, xpos + mousex, ypos + mousey);
					}
				} else if(mode == EDIT_CHARS || mode == EDIT_ITEMS) {
					if(event.button.button == SDL_BUTTON_RIGHT) {
						lvl.remove_characters_in_rect(anchorx, anchory, xpos + mousex, ypos + mousey);
					}
				} else if(mode == EDIT_GROUPS) {
					std::vector<entity_ptr> chars = lvl.get_characters_in_rect(rect::from_coordinates(anchorx, anchory, xpos + mousex, ypos + mousey));
					const int group = lvl.add_group();
					foreach(entity_ptr c, chars) {
						lvl.set_character_group(c, group);
					}
				} else if(mode == EDIT_PROPERTIES) {
					std::vector<entity_ptr> chars = lvl.get_characters_in_rect(rect::from_coordinates(anchorx, anchory, xpos + mousex, ypos + mousey));
					if(chars.empty() == false) {
						selected_entity = chars.front();
					}
				} else if(mode == EDIT_VARIATIONS) {
					const int xtile = (xpos + mousex)/TileSize;
					const int ytile = (ypos + mousey)/TileSize;
					lvl.flip_variations(xtile, ytile);
				}
				break;
			default:
				break;
			}
		}

		graphics::prepare_raster();
		lvl.draw_background(0,0,0);
		glPushMatrix();
		glTranslatef(-xpos,-ypos,0);

		if(zorder >= 0) {
			lvl.draw(xpos, ypos, graphics::screen_width(), graphics::screen_height());
		}

		if(zorder < 0) {
			lvl.draw(xpos, ypos, graphics::screen_width(), graphics::screen_height());
		}

		if(buttons) {
			int x1 = anchorx;
			int x2 = xpos + mousex;
			if(x1 > x2) {
				std::swap(x1,x2);
			}

			int y1 = anchory;
			int y2 = ypos + mousey;
			if(y1 > y2) {
				std::swap(y1,y2);
			}

			const SDL_Rect rect = {x1, y1, x2 - x1, y2 - y1};
			const SDL_Color color = {255,255,255,255};
			graphics::draw_hollow_rect(rect, color);
		}
		glPopMatrix();

		//draw grid
		glDisable(GL_TEXTURE_2D);
		glBegin(GL_LINES);
		glColor4ub(255, 255, 255, 64);
		for(int x = TileSize - xpos%TileSize; x < graphics::screen_width(); x += 32) {
			glVertex3f(x, 0, 0);
			glVertex3f(x, graphics::screen_height(), 0);
		}

		for(int y = TileSize - ypos%TileSize; y < graphics::screen_height(); y += 32) {
			glVertex3f(0, y, 0);
			glVertex3f(graphics::screen_width(), y, 0);
		}
		glColor4ub(255, 255, 255, 255);
		
		// draw level boundaries in clear white
		{
			const int x1 = lvl.boundaries().x();
			const int x2 = lvl.boundaries().x2();
			const int y1 = lvl.boundaries().y();
			const int y2 = lvl.boundaries().y2();
			glVertex3f(x1 - xpos, y1 - ypos, 0);
			glVertex3f(x2 - xpos, y1 - ypos, 0);

			glVertex3f(x1 - xpos, y1 - ypos, 0);
			glVertex3f(x1 - xpos, y2 - ypos, 0);

			glVertex3f(x2 - xpos, y1 - ypos, 0);
			glVertex3f(x2 - xpos, y2 - ypos, 0);

			glVertex3f(x1 - xpos, y2 - ypos, 0);
			glVertex3f(x2 - xpos, y2 - ypos, 0);
		}

		glEnd();
		glEnable(GL_TEXTURE_2D);

		if(mode == EDIT_TILES) {
			foreach(const level_tile& t, tileset_preview) {
				level_object::draw(t);
			}
		} else if(mode == EDIT_CHARS) {
			enemy_types[cur_enemy_type].preview_frame->draw(700, 10, face_right);
		} else if(mode == EDIT_ITEMS) {
			const_item_type_ptr type = placeable_items[cur_item].type;
			if(type) {
				const frame& f = type->get_frame();
				f.draw(700, 10, true);
			}
		} else if(mode == EDIT_PROPERTIES && selected_entity) {
			game_logic::formula_callable* vars = selected_entity->vars();
			if(vars) {
				std::vector<game_logic::formula_input> inputs;
				vars->get_inputs(&inputs);
				for(int n = 0; n != inputs.size(); ++n) {
					std::ostringstream s;
					s << "(" << (n+1) << ") " << inputs[n].name << ": " << vars->query_value(inputs[n].name).to_debug_string();
					glColor4ub(255, 255, 255, selected_property == n ? 255 : 160);
					graphics::blit_texture(font::render_text(s.str(), graphics::color_black(), 14), 600, 20 + n*20);
					glColor4ub(255, 255, 255, 255);

					if(inputs[n].name == "x_bound" || inputs[n].name == "x2_bound") {
						glDisable(GL_TEXTURE_2D);
						glBegin(GL_LINES);
						glColor4ub(255,0,0,128);
						const int value = vars->query_value(inputs[n].name).as_int();
						const int pos = value - xpos;
						glVertex3f(pos, 0, 0);
						glVertex3f(pos, graphics::screen_height(), 0);
						glColor4ub(255,255,255,255);
						glEnd();
						glEnable(GL_TEXTURE_2D);
					}

					if(inputs[n].name == "y_bound" || inputs[n].name == "y2_bound") {
						glDisable(GL_TEXTURE_2D);
						glBegin(GL_LINES);
						glColor4ub(255,0,0,128);
						const int value = vars->query_value(inputs[n].name).as_int();
						const int pos = value - ypos;
						glVertex3f(0, pos, 0);
						glVertex3f(graphics::screen_width(), pos, 0);
						glColor4ub(255,255,255,255);
						glEnd();
						glEnable(GL_TEXTURE_2D);
					}

				}
			}
		}

		//the location of the mouse cursor in the map
		char loc_buf[256];
		sprintf(loc_buf, "%d,%d", xpos + mousex, ypos + mousey);
		graphics::blit_texture(font::render_text(loc_buf, graphics::color_yellow(), 14), 10, 10);

		SDL_GL_SwapBuffers();
		SDL_Delay(20);
	}
}
