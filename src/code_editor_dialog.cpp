#ifndef NO_EDITOR
#include <boost/bind.hpp>

#include <algorithm>

#include "border_widget.hpp"
#include "button.hpp"
#include "code_editor_dialog.hpp"
#include "code_editor_widget.hpp"
#include "custom_object.hpp"
#include "custom_object_callable.hpp"
#include "custom_object_type.hpp"
#include "drag_widget.hpp"
#include "filesystem.hpp"
#include "font.hpp"
#include "foreach.hpp"
#include "formatter.hpp"
#include "formula_function_registry.hpp"
#include "frame.hpp"
#include "image_widget.hpp"
#include "json_parser.hpp"
#include "label.hpp"
#include "level.hpp"
#include "module.hpp"
#include "object_events.hpp"
#include "raster.hpp"
#include "surface_cache.hpp"
#include "text_editor_widget.hpp"
#include "tile_map.hpp"
#include "tileset_editor_dialog.hpp"
#include "unit_test.hpp"

std::set<level*>& get_all_levels_set();

code_editor_dialog::code_editor_dialog(const rect& r)
  : dialog(r.x(), r.y(), r.w(), r.h()), invalidated_(0), modified_(false),
    suggestions_prefix_(-1)
{
	init();
}

void code_editor_dialog::init()
{
	clear();

	using namespace gui;

	if(!editor_) {
		editor_.reset(new code_editor_widget(width() - 40, height() - 60));
	}

	button* save_button = new button("Save", boost::bind(&code_editor_dialog::save, this));
	button* increase_font = new button("+", boost::bind(&code_editor_dialog::change_font_size, this, 1));
	button* decrease_font = new button("-", boost::bind(&code_editor_dialog::change_font_size, this, -1));

	//std::cerr << "CED: " << x() << "," << y() << "; " << width() << "," << height() << std::endl;
	drag_widget* dragger = new drag_widget(x(), y(), width(), height(),
		drag_widget::DRAG_HORIZONTAL, NULL, 
		boost::bind(&code_editor_dialog::on_drag_end, this, _1, _2), 
		boost::bind(&code_editor_dialog::on_drag, this, _1, _2));

	search_ = new text_editor_widget(120);
	replace_ = new text_editor_widget(120);
	const SDL_Color col = {255,255,255,255};
	widget_ptr find_label(label::create("Find: ", col));
	replace_label_ = label::create("Replace: ", col);
	status_label_ = label::create("Ok", col);
	error_label_ = label::create("", col);
	add_widget(find_label, 42, 12, MOVE_RIGHT);
	add_widget(widget_ptr(search_), MOVE_RIGHT);
	add_widget(replace_label_, MOVE_RIGHT);
	add_widget(widget_ptr(replace_), MOVE_RIGHT);
	add_widget(widget_ptr(save_button), MOVE_RIGHT);
	add_widget(widget_ptr(increase_font), MOVE_RIGHT);
	add_widget(widget_ptr(decrease_font), MOVE_RIGHT);
	add_widget(editor_, find_label->x(), find_label->y() + save_button->height() + 2);
	add_widget(status_label_);
	add_widget(error_label_, status_label_->x() + 480, status_label_->y());
	add_widget(widget_ptr(dragger));

	replace_label_->set_visible(false);
	replace_->set_visible(false);

	search_->set_on_tab_handler(boost::bind(&code_editor_dialog::on_tab, this));
	replace_->set_on_tab_handler(boost::bind(&code_editor_dialog::on_tab, this));

	search_->set_on_change_handler(boost::bind(&code_editor_dialog::on_search_changed, this));
	search_->set_on_enter_handler(boost::bind(&code_editor_dialog::on_search_enter, this));
	replace_->set_on_enter_handler(boost::bind(&code_editor_dialog::on_replace_enter, this));


	init_files_grid();
}

void code_editor_dialog::init_files_grid()
{
	if(files_grid_) {
		remove_widget(files_grid_);
	}

	if(files_.empty()) {
		return;
	}
	
	using namespace gui;

	files_grid_.reset(new grid(1));
	files_grid_->allow_selection();
	files_grid_->register_selection_callback(boost::bind(&code_editor_dialog::select_file, this, _1));
	foreach(const KnownFile& f, files_) {
		if(f.anim) {
			image_widget* img = new image_widget(f.anim->img());
			img->set_dim(42, 42);
			img->set_area(f.anim->area());

			files_grid_->add_col(widget_ptr(img));
		} else {
			std::string fname = f.fname;
			while(std::find(fname.begin(), fname.end(), '/') != fname.end()) {
				fname.erase(fname.begin(), std::find(fname.begin(), fname.end(), '/')+1);
			}
			if(fname.size() > 6) {
				fname.resize(6);
			}

			files_grid_->add_col(label::create(fname, graphics::color_white()));
		}
	}

	add_widget(files_grid_, 2, 2);
}

void code_editor_dialog::load_file(std::string fname, bool focus)
{
	if(fname_ == fname) {
		return;
	}

	using namespace gui;

	int index = 0;
	foreach(const KnownFile& f, files_) {
		if(f.fname == fname) {
			break;
		}

		++index;
	}

	if(index == files_.size()) {
		KnownFile f;
		f.fname = fname;
		f.editor.reset(new code_editor_widget(width() - 40, height() - 60));
		std::string text = json::get_file_contents(fname);
		try {
			variant doc = json::parse(text, json::JSON_NO_PREPROCESSOR);
			std::cerr << "CHECKING FOR PROTOTYPES: " << doc["prototype"].write_json() << "\n";
			if(doc["prototype"].is_list()) {
				std::map<std::string,std::string> paths;
				module::get_unique_filenames_under_dir("data/object_prototypes", &paths);
				foreach(variant proto, doc["prototype"].as_list()) {
					std::string name = proto.as_string() + ".cfg";
					std::map<std::string,std::string>::const_iterator itor = module::find(paths, name);
					if(itor != paths.end()) {
						load_file(itor->second, false);
					}
				}
			}
		} catch(...) {
		}

		f.editor->set_text(json::get_file_contents(fname));
		f.editor->set_on_change_handler(boost::bind(&code_editor_dialog::on_code_changed, this));
		f.editor->set_on_move_cursor_handler(boost::bind(&code_editor_dialog::on_move_cursor, this));

		foreach(const std::string& obj_type, custom_object_type::get_all_ids()) {
			const std::string* path = custom_object_type::get_object_path(obj_type + ".cfg");
			if(path && *path == fname) {
				f.anim.reset(new frame(custom_object_type::get(obj_type)->default_frame()));
				break;
			}
		}

		//in case any files have been inserted, update the index.
		index = files_.size();
		files_.push_back(f);
	}

	KnownFile f = files_[index];

	if(editor_) {
		f.editor->set_font_size(editor_->get_font_size());
	}

	if(!focus) {
		return;
	}

	files_.erase(files_.begin() + index);
	files_.insert(files_.begin(), f);

	add_widget(f.editor, editor_->x(), editor_->y());
	remove_widget(editor_);

	editor_ = f.editor;
	editor_->set_focus(true);

	init_files_grid();

	fname_ = fname;

	modified_ = editor_->text() != sys::read_file(module::map_file(fname));
	on_move_cursor();
}

void code_editor_dialog::select_file(int index)
{
	if(index < 0 || index >= files_.size()) {
		return;
	}

	std::cerr << "select file " << index << " -> " << files_[index].fname << "\n";

	load_file(files_[index].fname);
}

bool code_editor_dialog::has_keyboard_focus() const
{
	return editor_->has_focus() || search_->has_focus() || replace_->has_focus();
}

bool code_editor_dialog::handle_event(const SDL_Event& event, bool claimed)
{
	if(animation_preview_) {
		claimed = animation_preview_->process_event(event, claimed) || claimed;
		if(claimed) {
			return claimed;
		}
	}

	if(suggestions_grid_) {
		//since an event could cause removal of the suggestions grid,
		//make sure the object doesn't get cleaned up until after the event.
		gui::widget_ptr suggestions = suggestions_grid_;
		claimed = suggestions->process_event(event, claimed) || claimed;
		if(claimed) {
			return claimed;
		}
	}

	claimed = claimed || dialog::handle_event(event, claimed);
	if(claimed) {
		return claimed;
	}

	if(has_keyboard_focus()) {
		switch(event.type) {
		case SDL_KEYDOWN: {
			if(event.key.keysym.sym == SDLK_f && (event.key.keysym.mod&KMOD_CTRL)) {
				search_->set_focus(true);
				replace_->set_focus(false);
				editor_->set_focus(false);
				return true;
			} else if(event.key.keysym.sym == SDLK_s && (event.key.keysym.mod&KMOD_CTRL)) {
				save();
				return true;
			} else if(event.key.keysym.sym == SDLK_TAB && (event.key.keysym.mod&KMOD_CTRL) && files_grid_) {
				if(!files_grid_->has_must_select()) {
					files_grid_->must_select(true, 1);
				} else {
					files_grid_->must_select(true, (files_grid_->selection()+1)%files_.size());
				}
			}
			break;
		}
		case SDL_KEYUP: {
			if(files_grid_ && files_grid_->has_must_select() && (event.key.keysym.sym == SDLK_LCTRL || event.key.keysym.sym == SDLK_RCTRL)) {
				select_file(files_grid_->selection());
			}
			break;
		}
		}
	}

	return claimed;
}

void code_editor_dialog::handle_draw_children() const
{
	dialog::handle_draw_children();
	if(animation_preview_) {
		animation_preview_->draw();
	}

	if(suggestions_grid_) {
		suggestions_grid_->draw();
	}
}

void code_editor_dialog::change_font_size(int amount)
{
	if(editor_) {
		editor_->change_font_size(amount);
	}
}

void code_editor_dialog::process()
{
	using namespace gui;

	if(invalidated_ && SDL_GetTicks() > invalidated_ + 200) {
		try {
			custom_object::reset_current_debug_error();

#if defined(USE_GLES2)
			gles2::shader::get_and_clear_runtime_error();
#endif


			if(strstr(fname_.c_str(), "/tiles/")) {
				std::cerr << "INIT TILE MAP\n";

				const std::string old_contents = json::get_file_contents(fname_);

				//verify the text is parseable before we bother setting it.
				json::parse(editor_->text());
				json::set_file_contents(fname_, editor_->text());
				const variant tiles_data = json::parse_from_file("data/tiles.cfg");
				tile_map::prepare_rebuild_all();
				try {
					std::cerr << "tile_map::init()\n";
					tile_map::init(tiles_data);
					tile_map::rebuild_all();
					std::cerr << "done tile_map::init()\n";
					editor_dialogs::tileset_editor_dialog::global_tile_update();
					foreach(level* lvl, get_all_levels_set()) {
						lvl->rebuild_tiles();
					}
				} catch(...) {
					json::set_file_contents(fname_, old_contents);
					const variant tiles_data = json::parse_from_file("data/tiles.cfg");
					tile_map::init(tiles_data);
					tile_map::rebuild_all();
					editor_dialogs::tileset_editor_dialog::global_tile_update();
					foreach(level* lvl, get_all_levels_set()) {
						lvl->rebuild_tiles();
					}
					throw;
				}
				std::cerr << "INIT TILE MAP OK\n";
#if defined(USE_GLES2)
			} else if(strstr(fname_.c_str(), "data/shaders.cfg")) {
				std::cerr << "CODE_EDIT_DIALOG FILE: " << fname_ << std::endl;
				gles2::program::load_shaders(editor_->text());
				foreach(level* lvl, get_all_levels_set()) {
					lvl->shaders_updated();
				}
#endif
			} else { 
				std::cerr << "SET FILE: " << fname_ << "\n";
				custom_object_type::set_file_contents(fname_, editor_->text());
			}
			error_label_->set_text("Ok");
			error_label_->set_tooltip("");
		} catch(validation_failure_exception& e) {
			error_label_->set_text("Error");
			error_label_->set_tooltip(e.msg);
		} catch(...) {
			error_label_->set_text("Error");
			error_label_->set_tooltip("Unknown error");
		}
		invalidated_ = 0;
	} else if(custom_object::current_debug_error()) {
		error_label_->set_text("Runtime Error");
		error_label_->set_tooltip(*custom_object::current_debug_error());
	}

#if defined(USE_GLES2)
	const std::string shader_error = gles2::shader::get_and_clear_runtime_error();
	if(shader_error != "") {
		error_label_->set_text("Runtime Shader Error");
		error_label_->set_tooltip(shader_error);
	}
#endif

	const bool show_replace = editor_->has_search_matches();
	replace_label_->set_visible(show_replace);
	replace_->set_visible(show_replace);

	const int cursor_pos = editor_->row_col_to_text_pos(editor_->cursor_row(), editor_->cursor_col());
	const std::string& text = editor_->current_text();

	const gui::code_editor_widget::ObjectInfo info = editor_->get_current_object();
	const json::Token* selected_token = NULL;
	int token_pos = 0;
	foreach(const json::Token& token, info.tokens) {
		const int begin_pos = token.begin - text.c_str();
		const int end_pos = token.end - text.c_str();
		if(cursor_pos >= begin_pos && cursor_pos <= end_pos) {
			token_pos = cursor_pos - begin_pos;
			selected_token = &token;
			break;
		}
	}

	std::vector<Suggestion> suggestions;
	if(selected_token != NULL) {

		const bool at_end = token_pos == selected_token->end - selected_token->begin;
		std::string str(selected_token->begin, selected_token->end);
		suggestions_prefix_ = 0;
		if(str.size() >= 3 && std::string(str.begin(), str.begin()+3) == "on_" && at_end) {
			const std::string id(str.begin()+3, str.end());
			for(int i = 0; i != NUM_OBJECT_BUILTIN_EVENT_IDS; ++i) {
				const std::string& event_str = get_object_event_str(i);
				if(event_str.size() >= id.size() && std::equal(id.begin(), id.end(), event_str.begin())) {
					Suggestion s = { "on_" + event_str, "", ": \"\",", 3 };
					suggestions.push_back(s);
				}
			}

			static std::vector<std::string> animations;

			if(info.obj.is_map() && info.obj["animation"].is_list()) {
				animations.clear();
				foreach(variant anim, info.obj["animation"].as_list()) {
					if(anim.is_map() && anim["id"].is_string()) {
						animations.push_back(anim["id"].as_string());
					}
				}
			}

			foreach(const std::string& str, animations) {
				static const std::string types[] = {"enter", "end", "leave", "process"};
				foreach(const std::string& type, types) {
					const std::string event_str = type + "_" + str + (type == "process" ? "" : "_anim");
					if(event_str.size() >= id.size() && std::equal(id.begin(), id.end(), event_str.begin())) {
						Suggestion s = { "on_" + event_str, "", ": \"\",", 3 };
						suggestions.push_back(s);
					}
				}
			}

			suggestions_prefix_ = str.size();
		} else if(selected_token->type == json::Token::TYPE_STRING) {
			try {
				const std::string formula_str(selected_token->begin, selected_token->end);
				std::vector<formula_tokenizer::token> tokens;
				std::string::const_iterator i1 = formula_str.begin();
				formula_tokenizer::token t = formula_tokenizer::get_token(i1, formula_str.end());
				while(t.type != formula_tokenizer::TOKEN_INVALID) {
					tokens.push_back(t);
					if(i1 == formula_str.end()) {
						break;
					}

					t = formula_tokenizer::get_token(i1, formula_str.end());
				}

				const formula_tokenizer::token* selected = NULL;
				const std::string::const_iterator itor = formula_str.begin() + token_pos;

				foreach(const formula_tokenizer::token& tok, tokens) {
					if(tok.end == itor) {
						selected = &tok;
						break;
					}
				}

				if(selected && selected->type == formula_tokenizer::TOKEN_IDENTIFIER) {
					const std::string identifier(selected->begin, selected->end);

					static const custom_object_callable obj_definition;
					for(int n = 0; n != obj_definition.num_slots(); ++n) {
						const std::string id = obj_definition.get_entry(n)->id;
						if(id.size() > identifier.size() && std::equal(identifier.begin(), identifier.end(), id.begin())) {
							Suggestion s = { id, "", "", 0 };
							suggestions.push_back(s);
						}
					}

					std::vector<std::string> helpstrings;
					foreach(const std::string& s, function_helpstrings("core")) {
						helpstrings.push_back(s);
					}
					foreach(const std::string& s, function_helpstrings("custom_object")) {
						helpstrings.push_back(s);
					}

					foreach(const std::string& str, helpstrings) {
						std::string::const_iterator paren = std::find(str.begin(), str.end(), '(');
						std::string::const_iterator colon = std::find(paren, str.end(), ':');
						if(colon == str.end()) {
							continue;
						}

						const std::string id(str.begin(), paren);
						const std::string text(str.begin(), colon);
						if(id.size() > identifier.size() && std::equal(identifier.begin(), identifier.end(), id.begin())) {
							Suggestion s = { id, text, "()", 1 };
							suggestions.push_back(s);
						}
					}

					suggestions_prefix_ = identifier.size();
				}
			} catch(formula_tokenizer::token_error&) {
			}
		}

	}

	std::sort(suggestions.begin(), suggestions.end());

	if(suggestions != suggestions_) {
		suggestions_ = suggestions;
		suggestions_grid_.reset();

		if(suggestions_.empty() == false) {
			grid_ptr suggestions_grid(new grid(1));
			suggestions_grid->register_selection_callback(boost::bind(&code_editor_dialog::select_suggestion, this, _1));
			suggestions_grid->swallow_clicks();
			suggestions_grid->allow_selection(true);
			suggestions_grid->set_show_background(true);
			suggestions_grid->set_max_height(160);
			foreach(const Suggestion& s, suggestions_) {
				suggestions_grid->add_col(widget_ptr(new label(s.suggestion_text.empty() ? s.suggestion : s.suggestion_text)));
			}

			suggestions_grid_.reset(new border_widget(suggestions_grid, graphics::color(255,255,255,255)));
		}
		std::cerr << "SUGGESTIONS: " << suggestions_.size() << ":\n";
		foreach(const Suggestion& suggestion, suggestions_) {
			std::cerr << " - " << suggestion.suggestion << "\n";
		}
	}

	if(suggestions_grid_) {
		const std::pair<int,int> cursor_pos = editor_->char_position_on_screen(editor_->cursor_row(), editor_->cursor_col());
		suggestions_grid_->set_loc(x() + editor_->x() + cursor_pos.second, y() + editor_->y() + cursor_pos.first - suggestions_grid_->height());
		
		if(suggestions_grid_->y() < 10) {
			suggestions_grid_->set_loc(suggestions_grid_->x(), suggestions_grid_->y() + suggestions_grid_->height() + 14);
		}

		if(suggestions_grid_->x() + suggestions_grid_->width() + 20 > graphics::screen_width()) {
			suggestions_grid_->set_loc(graphics::screen_width() - suggestions_grid_->width() - 20, suggestions_grid_->y());
		}
	}

	try {
		editor_->set_highlight_current_object(false);
		if(gui::animation_preview_widget::is_animation(info.obj)) {
			if(!animation_preview_) {
				animation_preview_.reset(new gui::animation_preview_widget(info.obj));
				animation_preview_->set_rect_handler(boost::bind(&code_editor_dialog::set_animation_rect, this, _1));
				animation_preview_->set_solid_handler(boost::bind(&code_editor_dialog::move_solid_rect, this, _1, _2));
				animation_preview_->set_pad_handler(boost::bind(&code_editor_dialog::set_integer_attr, this, "pad", _1));
				animation_preview_->set_num_frames_handler(boost::bind(&code_editor_dialog::set_integer_attr, this, "frames", _1));
				animation_preview_->set_frames_per_row_handler(boost::bind(&code_editor_dialog::set_integer_attr, this, "frames_per_row", _1));
				animation_preview_->set_loc(x() - 520, y() + 100);
				animation_preview_->set_dim(500, 400);
				animation_preview_->init();
			} else {
				animation_preview_->set_object(info.obj);
			}

			editor_->set_highlight_current_object(true);
		} else {
			animation_preview_.reset();
		}
	} catch(type_error&) {
		if(animation_preview_) {
			animation_preview_.reset();
		}
	} catch(frame::error&) {
		// skip
	} catch(validation_failure_exception&) {
		if(animation_preview_) {
			animation_preview_.reset();
		}
	} catch(graphics::load_image_error&) {
		if(animation_preview_) {
			animation_preview_.reset();
		}
	}

	if(animation_preview_) {
		animation_preview_->process();
	}

}

void code_editor_dialog::change_width(int amount)
{
	int new_width = width() + amount;
	if(new_width < 200) {
		new_width = 200;
	}

	if(new_width > 1000) {
		new_width = 1000;
	}

	amount = new_width - width();
	set_loc(x() - amount, y());
	set_dim(new_width, height());


	foreach(KnownFile& f, files_) {
		f.editor->set_dim(width() - 40, height() - 60);
	}
	init();
}

void code_editor_dialog::on_drag(int dx, int dy) 
{
	int new_width = width() + dx;
	int min_width = int(graphics::screen_width() * 0.17);
	int max_width = int(graphics::screen_width() * 0.83);
	//std::cerr << "ON_DRAG: " << dx << ", " << min_width << ", " << max_width << std::endl;
	if(new_width < min_width) { 
		new_width = min_width;
	}

	if(new_width > max_width) {
		new_width = max_width;
	}

	dx = new_width - width();
	set_loc(x() - dx, y());
	set_dim(new_width, height());


	foreach(KnownFile& f, files_) {
		f.editor->set_dim(width() - 40, height() - 60);
	}
	//init();
}

void code_editor_dialog::on_drag_end(int x, int y)
{
	init();
}

void code_editor_dialog::on_tab()
{
	if(search_->has_focus()) {
		search_->set_focus(false);
		if(editor_->has_search_matches()) {
			replace_->set_focus(true);
		} else {
			editor_->set_focus(true);
		}
	} else if(replace_->has_focus()) {
		replace_->set_focus(false);
		editor_->set_focus(true);
	}
}

void code_editor_dialog::on_search_changed()
{
	editor_->set_search(search_->text());
}

void code_editor_dialog::on_search_enter()
{
	editor_->next_search_match();
}

void code_editor_dialog::on_replace_enter()
{
	editor_->replace(replace_->text());
}

void code_editor_dialog::on_code_changed()
{
	if(!modified_) {
		modified_ = true;
		on_move_cursor();
	}

	if(!invalidated_) {
		invalidated_ = SDL_GetTicks();
		error_label_->set_text("Processing...");
	}
}

void code_editor_dialog::on_move_cursor()
{
	status_label_->set_text(formatter() << "Line " << (editor_->cursor_row()+1) << " Col " << (editor_->cursor_col()+1) << (modified_ ? " (Modified)" : ""));
}

void code_editor_dialog::set_animation_rect(rect r)
{
	const gui::code_editor_widget::ObjectInfo info = editor_->get_current_object();
	variant v = info.obj;
	if(v.is_null() == false) {
		v.add_attr(variant("rect"), r.write());
		editor_->modify_current_object(v);
		try {
			animation_preview_->set_object(v);
		} catch(frame::error& e) {
		}
	}
}

void code_editor_dialog::move_solid_rect(int dx, int dy)
{
	const gui::code_editor_widget::ObjectInfo info = editor_->get_current_object();
	variant v = info.obj;
	if(v.is_null() == false) {
		variant solid_area = v["solid_area"];
		if(!solid_area.is_list() || solid_area.num_elements() != 4) {
			return;
		}

		foreach(const variant& num, solid_area.as_list()) {
			if(!num.is_int()) {
				return;
			}
		}

		rect area(solid_area);
		area = rect(area.x() + dx, area.y() + dy, area.w(), area.h());
		v.add_attr(variant("solid_area"), area.write());
		editor_->modify_current_object(v);
		try {
			animation_preview_->set_object(v);
		} catch(frame::error& e) {
		}
	}
}

void code_editor_dialog::set_integer_attr(const char* attr, int value)
{
	const gui::code_editor_widget::ObjectInfo info = editor_->get_current_object();
	variant v = info.obj;
	if(v.is_null() == false) {
		v.add_attr(variant(attr), variant(value));
		editor_->modify_current_object(v);
		try {
			animation_preview_->set_object(v);
		} catch(frame::error& e) {
		}
	}
}

void code_editor_dialog::save()
{
	sys::write_file(module::map_file(fname_), editor_->text());
	status_label_->set_text(formatter() << "Saved " << fname_);
	modified_ = false;
}

void code_editor_dialog::select_suggestion(int index)
{
	if(index >= 0 && index < suggestions_.size()) {
		std::cerr << "SELECT " << suggestions_[index].suggestion << "\n";
		const std::string& str = suggestions_[index].suggestion;
		if(suggestions_prefix_ >= 0 && suggestions_prefix_ < str.size()) {
			const int col = editor_->cursor_col();
			const std::string insert(str.begin() + suggestions_prefix_, str.end());
			const std::string postfix = suggestions_[index].postfix;
			const std::string row = editor_->get_data()[editor_->cursor_row()];
			const std::string new_row = std::string(row.begin(), row.begin() + editor_->cursor_col()) + insert + postfix + std::string(row.begin() + editor_->cursor_col(), row.end());
			editor_->set_row_contents(editor_->cursor_row(), new_row);
			editor_->set_cursor(editor_->cursor_row(), col + insert.size() + suggestions_[index].postfix_index);
		}
	} else {
		suggestions_grid_.reset();
	}
}

COMMAND_LINE_UTILITY(codeedit)
{
	SDL_Init(SDL_INIT_VIDEO);
	SDL_SetVideoMode(600, 600, 0, SDL_OPENGL|SDL_RESIZABLE);
#ifdef USE_GLES2
	glViewport(0, 0, 600, 600);
	glEnable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#else
	glShadeModel(GL_SMOOTH);
	glEnable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif

	const font::manager font_manager;
	graphics::texture::manager texture_manager;

	variant gui_node = json::parse_from_file(module::map_file("data/gui.cfg"));
	gui_section::init(gui_node);

	framed_gui_element::init(gui_node);

	code_editor_dialog d(rect(0,0,600,600));
	std::cerr << "CREATE DIALOG\n";
	if(args.empty() == false) {
		d.load_file(args[0]);
	}

	d.show_modal();


	SDL_Quit();
}

#endif // NO_EDITOR
