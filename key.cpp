/* $Id: key.cpp 19552 2007-08-15 13:41:56Z mordante $ */
/*
   Copyright (C) 2003 - 2007 by David White <dave@whitevine.net>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2
   or at your option any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#include "key.hpp"

CKey::CKey() : is_enabled(true)
{
	static int num_keys = 300;
	key_list = SDL_GetKeyState( &num_keys );
}

int CKey::operator[]( int code ) const
{
	return (code == SDLK_ESCAPE || is_enabled) && int(key_list[code]);
}

void CKey::SetEnabled( bool enable )
{
	is_enabled = enable;
}
