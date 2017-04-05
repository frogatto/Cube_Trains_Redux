#ifndef NO_EDITOR
#include <boost/bind.hpp>

#include <algorithm>

#include "button.hpp"
#include "code_editor_dialog.hpp"
#include "code_editor_widget.hpp"
#include "custom_object_type.hpp"
#include "filesystem.hpp"
#include "foreach.hpp"
#include "formatter.hpp"
#include "frame.hpp"
#include "image_widget.hpp"
#include "json_parser.hpp"
#include "label.hpp"
#include "module.hpp"
#include "object_events.hpp"
#include "raster.hpp"
#include "text_editor_widget.hpp"

code_editor_dialog::code_editor_dialog(const rect& r)
  : dialog(r.x(), r.y(), r.w(), r.h()), invalidated_(0), modified_(false)
{
	init();
}

void code_editor_dialog::init()
{
	using namespace gui;

	button* save_button = new button("Save", boost::bind(&code_editor_dialog::save, this));

	editor_.reset(new code_editor_widget(width() - 40, height() - 60));
	search_ = new text_editor_widget(120);
	replace_ = new text_editor_widget(120);
	const SDL_Color col = {255,255,255,255};
	widget_ptr find_label(label::create("Find: ", col));
	status_label_ = label::create("Ok", col);
	error_label_ = label::create("", col);
	add_widget(find_label, 42, 12, MOVE_RIGHT);
	add_widget(widget_ptr(search_), MOVE_RIGHT);
	add_widget(widget_ptr(replace_), MOVE_RIGHT);
	add_widget(widget_ptr(save_button), MOVE_RIGHT);
	add_widget(editor_, find_label->x(), find_label->y() + save_button->height() + 2);
	add_widget(status_label_);
	add_widget(error_label_, status_label_->x() + 480, status_label_->y());

	replace_->set_visible(false);

	search_->set_on_change_handler(boost::bind(&code_editor_dialog::on_search_changed, this));
	search_->set_on_enter_handler(boost::bind(&code_editor_dialog::on_search_enter, this));


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
			if(fname.size() > 4) {
				fname.resize(4);
			}

			files_grid_->add_col(label::create(fname, graphics::color_white()));
		}
	}

	add_widget(files_grid_, 2, 2);
}

void code_editor_dialog::load_file(std::string fname)
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
		f.editor->set_text(json::get_file_contents(fname));
		f.editor->set_on_change_handler(boost::bind(&code_editor_dialog::on_code_changed, this));
		f.editor->set_on_move_cursor_handler(boost::bind(&code_editor_dialog::on_move_cursor, this));

		foreach(const_custom_object_type_ptr obj_type, custom_object_type::get_all()) {
			const std::string* path = custom_object_type::get_object_path(obj_type->id() + ".cfg");
			if(path && *path == fname) {
				f.anim.reset(new frame(obj_type->default_frame()));
				break;
			}
		}

		files_.push_back(f);
	}

	KnownFile f = files_[index];
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
		claimed = suggestions_grid_->process_event(event, claimed) || claimed;
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

void code_editor_dialog::process()
{
	using namespace gui;

	if(invalidated_ && SDL_GetTicks() > invalidated_ + 200) {
		const bool result = custom_object_type::set_file_contents(fname_, editor_->text());
		error_label_->set_text(result ? "Ok" : "Error");
		invalidated_ = 0;
	}

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

	std::vector<std::string> suggestions;
	if(selected_token != NULL) {

		const bool at_end = token_pos == selected_token->end - selected_token->begin;
		std::string str(selected_token->begin, selected_token->end);
		if(str.size() >= 3 && std::string(str.begin(), str.begin()+3) == "on_" && at_end) {
			const std::string id(str.begin()+3, str.end());
			for(int i = 0; i != NUM_OBJECT_BUILTIN_EVENT_IDS; ++i) {
				const std::string& event_str = get_object_event_str(i);
				if(event_str.size() > id.size() && std::equal(id.begin(), id.end(), event_str.begin())) {
					suggestions.push_back("on_" + event_str);
				}
			}
		}


	}

	if(suggestions != suggestions_) {
		suggestions_ = suggestions;
		suggestions_grid_.reset();

		if(suggestions_.empty() == false) {
			suggestions_grid_.reset(new grid(1));
			suggestions_grid_->set_show_background(true);
			suggestions_grid_->set_max_height(100);
			foreach(const std::string& str, suggestions_) {
				suggestions_grid_->add_col(widget_ptr(new label(str)));
			}
		}
		std::cerr << "SUGGESTIONS: " << suggestions_.size() << ":\n";
		foreach(const std::string& suggestion, suggestions_) {
			std::cerr << " - " << suggestion << "\n";
		}
	}

	if(suggestions_grid_) {
		const std::pair<int,int> cursor_pos = editor_->char_position_on_screen(editor_->cursor_row(), editor_->cursor_col());
		suggestions_grid_->set_loc(x() + editor_->x() + cursor_pos.second, y() + editor_->y() + cursor_pos.first - suggestions_grid_->height());
		/*
		if(suggestions_grid_->y() < 10) {
			suggestions_grid_->set_loc(suggestions_grid_->x(), suggestions_grid_->y() + suggestions_grid_->height() + 14);
		}

		if(suggestions_grid_->x() + suggestions_grid_->width() + 20 > graphics::screen_width()) {
			suggestions_grid_->set_loc(graphics::screen_width() - suggestions_grid_->width() - 20, suggestions_grid_->y());
		}*/
	}

	try {
		editor_->set_highlight_current_object(false);
		if(gui::animation_preview_widget::is_animation(info.obj)) {
			if(!animation_preview_) {
				animation_preview_.reset(new gui::animation_preview_widget(info.obj));
				animation_preview_->set_rect_handler(boost::bind(&code_editor_dialog::set_animation_rect, this, _1));
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
	} catch(type_error& e) {
		if(animation_preview_) {
			animation_preview_.reset();
		}
	} catch(frame::error& e) {
		if(animation_preview_) {
			animation_preview_.reset();
		}
	} catch(validation_failure_exception& e) {
		if(animation_preview_) {
			animation_preview_.reset();
		}
	}

	if(animation_preview_) {
		animation_preview_->process();
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
	status_label_->set_text(formatter() << "Row " << (editor_->cursor_row()+1) << " Col " << (editor_->cursor_col()+1) << (modified_ ? " (Modified)" : ""));
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

#endif // NO_EDITOR
