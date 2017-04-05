
/*
   Copyright (C) 2007 by David White <dave@whitevine.net>
   Part of the Silver Tree Project

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 or later.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>

#include "asserts.hpp"
#include "filesystem.hpp"
#include "string_utils.hpp"
#include "thread.hpp"
#include "unit_test.hpp"

#include <fstream>
#include <sstream>

#if defined(__native_client__)
#include "foreach.hpp"
#include "json_parser.hpp"
#include "variant.hpp"
#endif

// Include files for opendir(3), readdir(3), etc.
// These files may vary from platform to platform,
// since these functions are NOT ANSI-conforming functions.
// They may have to be altered to port to new platforms
#include <sys/types.h>

//for mkdir
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef __linux__
#include <sys/inotify.h>
#include <sys/select.h>
#endif

#ifdef _WIN32

/* /////////////////////////////////////////////////////////////////////////
 * This code swiped from dirent.c in the unixem library, version 1.7.3.
 * See http://synesis.com.au/software/unixem.html for full sources.
 * It's under BSD license.
 */

#include <direct.h>
#include <io.h>
#include <errno.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* /////////////////////////////////////////////////////////////////////////
 * Compiler differences
 */

#if defined(__BORLANDC__)
# define DIRENT_PROVIDED_BY_COMPILER
#elif defined(__DMC__)
# define DIRENT_PROVIDED_BY_COMPILER
#elif defined(__GNUC__)
# define DIRENT_PROVIDED_BY_COMPILER
#elif defined(__INTEL_COMPILER)
#elif defined(_MSC_VER)
#elif defined(__MWERKS__)
#elif defined(__WATCOMC__)
#else
# error Compiler not discriminated
#endif /* compiler */

#if defined(DIRENT_PROVIDED_BY_COMPILER)
#include <dirent.h>
#else

/* ////////////////////////////////////////////////////////////////////// */

#include <stddef.h>

#ifndef NAME_MAX
# define NAME_MAX   (260)
#endif /* !NAME_MAX */

struct dirent
{
	char	d_name[NAME_MAX + 1];	   /*!< file name (null-terminated) */
	int	d_mode;
};

struct DIR
{
	char			  directory[_MAX_DIR+1];   /* . */
	WIN32_FIND_DATAA  find_data;			   /* The Win32 FindFile data. */
	HANDLE			  hFind;				   /* The Win32 FindFile handle. */
	struct dirent	  dirent;				   /* The handle's entry. */
};

DIR *opendir(char const *name);
int closedir(DIR *dir);

#ifndef FILE_ATTRIBUTE_ERROR
# define FILE_ATTRIBUTE_ERROR			(0xFFFFFFFF)
#endif /* FILE_ATTRIBUTE_ERROR */

/* /////////////////////////////////////////////////////////////////////////
 * Helper functions
 */

static HANDLE dirent__findfile_directory(char const *name, LPWIN32_FIND_DATAA data)
{
	char	search_spec[_MAX_PATH +1];

	// Simply add the *.*, ensuring the path separator is included.
	(void)lstrcpyA(search_spec, name);
	if( '\\' != search_spec[lstrlenA(search_spec) - 1] &&
		'/' != search_spec[lstrlenA(search_spec) - 1])
	{
		(void)lstrcatA(search_spec, "\\*.*");
	}
	else
	{
		(void)lstrcatA(search_spec, "*.*");
	}

	return FindFirstFileA(search_spec, data);
}

/* /////////////////////////////////////////////////////////////////////////
 * API functions
 */

DIR *opendir(char const *name)
{
	DIR	*result =	NULL;
	DWORD	dwAttr;

	// Must be a valid name
	if( !name ||
		!*name ||
		(dwAttr = GetFileAttributesA(name)) == 0xFFFFFFFF)
	{
		errno = ENOENT;
	}
	// Must be a directory
	else if(!(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
	{
		errno = ENOTDIR;
	}
	else
	{
		result = (DIR*)malloc(sizeof(DIR));

		if(result == NULL)
		{
			errno = ENOMEM;
		}
		else
		{
			result->hFind=dirent__findfile_directory(name, &result->find_data);

			if(result->hFind == INVALID_HANDLE_VALUE)
			{
				free(result);

				result = NULL;
			}
			else
			{
				// Save the directory, in case of rewind.
				(void)lstrcpyA(result->directory, name);
				(void)lstrcpyA(result->dirent.d_name, result->find_data.cFileName);
				result->dirent.d_mode	=	(int)result->find_data.dwFileAttributes;
			}
		}
	}

	return result;
}

int closedir(DIR *dir)
{
	int ret;

	if(dir == NULL)
	{
		errno = EBADF;

		ret = -1;
	}
	else
	{
		// Close the search handle, if not already done.
		if(dir->hFind != INVALID_HANDLE_VALUE)
		{
			(void)FindClose(dir->hFind);
		}

		free(dir);

		ret = 0;
	}

	return ret;
}

struct dirent *readdir(DIR *dir)
{
	// The last find exhausted the matches, so return NULL.
	if(dir->hFind == INVALID_HANDLE_VALUE)
	{
		if(FILE_ATTRIBUTE_ERROR == dir->find_data.dwFileAttributes)
		{
			errno = EBADF;
		}
		else
		{
			dir->find_data.dwFileAttributes = FILE_ATTRIBUTE_ERROR;
		}

		return NULL;
	}
	else
	{
		// Copy the result of the last successful match to dirent.
		(void)lstrcpyA(dir->dirent.d_name, dir->find_data.cFileName);

		// Attempt the next match.
		if(!FindNextFileA(dir->hFind, &dir->find_data))
		{
			// Exhausted all matches, so close and null the handle.
			(void)FindClose(dir->hFind);
			dir->hFind = INVALID_HANDLE_VALUE;
		}

		return &dir->dirent;
	}
}

/*
 * Microsoft C uses _stat instead of stat,
 * for both the function name and the structure name.
 * See <http://svn.ghostscript.com:8080/ghostscript/trunk/gs/src/stat_.h>
 */
#ifdef _MSC_VER
#  define stat _stat
namespace {
	typedef int mode_t;
}
#endif

#ifndef S_IFMT
#define S_IFMT	(S_IFDIR|S_IFREG)
#endif
#ifndef S_ISREG
#define S_ISREG(x) (((x) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(x) (((x) & S_IFMT) == S_IFDIR)
#endif

#endif /* !DIRENT_PROVIDED_BY_COMPILER */

#define mkdir(a,b) (_mkdir(a))

bool isWinXpOrLater()
{
    OSVERSIONINFO osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&osvi);
    return ((osvi.dwMajorVersion > 5) || ((osvi.dwMajorVersion == 5) && (osvi.dwMinorVersion >= 1)));
}

#else /* !_WIN32 */

#include <unistd.h>

#include <dirent.h>

#endif /* !_WIN32 */

#ifdef __BEOS__
#include <Directory.h>
#include <FindDirectory.h>
#include <Path.h>
BPath be_path;
#endif

// for getenv
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>

namespace sys
{

namespace {
#ifdef HAVE_CONFIG_H
  const std::string data_dir=DATADIR ;
  const bool have_datadir = true;
#else
  const std::string data_dir="";
  const bool have_datadir = false;
#endif

  const mode_t AccessMode = 00770;
}

#if defined(__native_client__)
namespace {
variant& get_file_system() 
{
	static variant fs;
	return fs;
}

void load_file_system()
{
	get_file_system() = json::parse_from_file("/frogatto/filelist.json");
}
}
#endif


bool is_directory(const std::string& dname)
{
#if defined(__native_client__)
	if(get_file_system().is_null()) {
		load_file_system();
	}
	if(get_file_system()["paths"][dname].is_null()) {
		return false;
	}
	return true;
#else
	DIR* dir = opendir(dname.c_str());
	if(dir == NULL) {
		return false;
	}
	closedir(dir);
	return true;
#endif
}

void get_files_in_dir(const std::string& directory,
					  std::vector<std::string>* files,
					  std::vector<std::string>* dirs,
					  FILE_NAME_MODE mode)
{
	//std::cerr << "get_files_in_dir() : " << directory;
#if defined(__native_client__)
	if(get_file_system().is_null()) {
		load_file_system();
	}
	variant path;
	std::string dir_path = directory;
	if(directory.empty()) {
		// root folder.
		path = get_file_system();
	} else {
		if(directory[directory.length()-1] == '/') {
			dir_path = directory.substr(0, directory.length()-1);
		} else {
			dir_path = directory;
		}
		path = get_file_system()["paths"][dir_path];
	}
	if(path.is_null()) {
		return;
	}
	if(files != NULL) {
		for(int i = 0; i < path["files"].num_elements(); i++) {
			if(mode == ENTIRE_FILE_PATH && dir_path.empty() == false) {
				files->push_back(dir_path + "/" + path["files"][i].as_string());
			} else {
				files->push_back(path["files"][i].as_string());
			}
		}
	}
	if(dirs != NULL) {
		for(int i = 0; i < path["dirs"].num_elements(); i++) {
			if(mode == ENTIRE_FILE_PATH && dir_path.empty() == false) {
				dirs->push_back(dir_path + "/" + path["dirs"][i].as_string());
			} else {
				dirs->push_back(path["dirs"][i].as_string());
			}
		}
	}
#else
	struct stat st;

	DIR* dir = opendir(directory.c_str());

	if(dir == NULL) {
		return;
	}

	struct dirent* entry;
	while((entry = readdir(dir)) != NULL) {
		if(entry->d_name[0] == '.')
			continue;
#ifdef __APPLE__
		// HFS Mac OS X decomposes filenames using combining unicode characters.
		// Try to get the precomposed form.
		char basename[MAXNAMLEN+1];
		CFStringRef cstr = CFStringCreateWithCString(NULL,
							 entry->d_name,
							 kCFStringEncodingUTF8);
		CFMutableStringRef mut_str = CFStringCreateMutableCopy(NULL,
							 0, cstr);
		CFStringNormalize(mut_str, kCFStringNormalizationFormC);
		CFStringGetCString(mut_str,
				basename,sizeof(basename)-1,
				kCFStringEncodingUTF8);
		CFRelease(cstr);
		CFRelease(mut_str);
#else
		// generic Unix
		char *basename = entry->d_name;
#endif /* !APPLE */

		std::string fullname;
		if (directory.empty() || directory[directory.size()-1] == '/'
#ifdef __AMIGAOS4__
			|| (directory[directory.size()-1]==':')
#endif /* __AMIGAOS4__ */
		)
			fullname = directory + basename;
		else
			fullname = (directory + "/") + basename;

		if (::stat(fullname.c_str(), &st) != -1) {
			if (S_ISREG(st.st_mode)) {
				if (files != NULL) {
					if (mode == ENTIRE_FILE_PATH) {
						files->push_back(fullname);
					} else {
						files->push_back(basename);
					}
				}
			} else if (S_ISDIR(st.st_mode)) {
				if (dirs != NULL) {
					if (mode == ENTIRE_FILE_PATH) {
						dirs->push_back(fullname);
					} else {
						dirs->push_back(basename);
					}
				}
			}
		}
	}

	closedir(dir);
#endif

	if(files != NULL)
		std::sort(files->begin(),files->end());

	if (dirs != NULL)
		std::sort(dirs->begin(),dirs->end());
}

void get_unique_filenames_under_dir(const std::string& dir,
                                    std::map<std::string, std::string>* file_map,
									const std::string& prefix)
{
	if(dir.size() > 1024) {
		return;
	}

	std::vector<std::string> files;
	std::vector<std::string> dirs;
	get_files_in_dir(dir, &files, &dirs);
	for(std::vector<std::string>::const_iterator i = files.begin();
	    i != files.end(); ++i) {
		std::string sep = "/";
		if(dir[dir.length()-1] == '/') {
			sep = "";
		}
		(*file_map)[prefix + *i] = dir + sep + *i;
	}

	for(std::vector<std::string>::const_iterator i = dirs.begin();
	    i != dirs.end(); ++i) {
		std::string sep = "/";
		if(dir[dir.length()-1] == '/') {
			sep = "";
		}
		get_unique_filenames_under_dir(dir + sep + *i, file_map, prefix);
	}
}

std::string get_dir(const std::string& dir_path)
{
	std::cerr << "get_dir(): " << dir_path << std::endl;
	DIR* dir = opendir(dir_path.c_str());
	if(dir == NULL) {
		const int res = mkdir(dir_path.c_str(),AccessMode);
		if(res == 0) {
			dir = opendir(dir_path.c_str());
		} else {
			std::cerr << "could not open or create directory: " << dir_path << '\n';
		}
	}

	if(dir == NULL)
		return "";

	closedir(dir);

	return dir_path;
}

std::string get_user_data_dir()
{
#ifdef _WIN32

	static bool inited_dirs = false;

	if(!inited_dirs) {
		_mkdir("userdata");
		_mkdir("userdata/saves");
		_mkdir("dlc");
		inited_dirs = true;
	}

	char buf[256];
	const char* const res = getcwd(buf,sizeof(buf));

	if(res != NULL) {
		std::string cur_path(res);
		std::replace(cur_path.begin(),cur_path.end(),'\\','/');
		return cur_path + "/userdata";
	} else {
		return "userdata";
	}
#elif defined(__BEOS__)
	if (be_path.InitCheck() != B_OK) {
		BPath tpath;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &be_path, true) == B_OK) {
			be_path.Append("frogatto");
		} else {
			be_path.SetTo("/boot/home/config/settings/frogatto");
		}
		tpath = be_path;
		create_directory(tpath.Path(), 0775);
	}
	return be_path.Path();
#else

#ifndef __AMIGAOS4__
	static const char* const current_dir = ".";
	const char* home_str = getenv("HOME");
#elif defined(TARGET_PANDORA)
	static const char* const current_dir = ".";
	const char* home_str = getenv("PWD");
#elif defined(TARGET_BLACKBERRY)
	static const char* const current_dir = ".";
	const char* home_str = getenv("HOME");
#else
	static const char* const current_dir = " ";
	const char* home_str = "PROGDIR:";
#endif
	if(home_str == NULL)
		home_str = current_dir;

	const std::string home(home_str);

#ifndef PREFERENCES_DIR
#define PREFERENCES_DIR ".silvertree"
#endif

#ifndef __AMIGAOS4__
	const std::string dir_path = home + std::string("/") + PREFERENCES_DIR;
#else
	const std::string dir_path = home + PREFERENCES_DIR;
#endif
	DIR* dir = opendir(dir_path.c_str());
	if(dir == NULL) {
		const int res = mkdir(dir_path.c_str(),AccessMode);

		// Also create the maps directory
		mkdir((dir_path + "/editor").c_str(),AccessMode);
		mkdir((dir_path + "/saves").c_str(),AccessMode);
		mkdir((dir_path + "/dlc").c_str(),AccessMode);
		if(res == 0) {
			dir = opendir(dir_path.c_str());
		} else {
			std::cerr << "could not open or create directory: " << dir_path << '\n';
		}
	}

	if(dir == NULL)
		return "";

	closedir(dir);

	return dir_path;
#endif
}

std::string get_saves_dir()
{
	const std::string dir_path = get_user_data_dir() + "/saves";
	return get_dir(dir_path);
}

bool do_file_exists(const std::string& fname)
{
	std::ifstream file(fname.c_str(), std::ios_base::binary);
	if(file.rdstate() != 0) {
		return false;
	}

	file.close();
	return true;
}

std::string find_file(const std::string& fname)
{
	if(do_file_exists(fname)) {
		return fname;
	}
	if(have_datadir) {
		std::string data_fname = data_dir + "/" + fname;
		if(do_file_exists(data_fname)) {
			return data_fname;
		}
	}
	return fname;
}

int64_t file_mod_time(const std::string& fname)
{
	struct stat buf;
	if(stat(fname.c_str(), &buf)) {
		std::cerr << "file_mod_time FAILED for '" << fname << "': ";
		switch(errno) {
		case EACCES: std::cerr << "EACCES\n"; break;
		case EBADF: std::cerr << "EBADF\n"; break;
		case EFAULT: std::cerr << "EFAULT\n"; break;
		case ENOENT: std::cerr << "ENOENT\n"; break;
		case ENOTDIR: std::cerr << "ENOTDIR\n"; break;
		default: std::cerr << "UNKNOWN ERROR " << errno << "\n"; break;
		}

		return 0;
	}
	return static_cast<int64_t>(buf.st_mtime);
}

bool file_exists(const std::string& name)
{
	return do_file_exists(find_file(name));
}

std::string read_file(const std::string& name)
{
	std::string fname = find_file(name);
	std::ifstream file(fname.c_str(),std::ios_base::binary);
	std::stringstream ss;
	ss << file.rdbuf();
	return ss.str();
}

void write_file(const std::string& fname, const std::string& data)
{
	bool absolute_path = fname[0] == '/' ? true : false;
	//Try to ensure that the dir the file is in exists.
	std::vector<std::string> components = util::split(fname, '/');
	if(!components.empty()) {
		components.pop_back();

		std::vector<std::string> tmp;
		while(components.empty() == false 
			&& get_dir((absolute_path ? "/" : "") + util::join(components, '/')).empty()) {
			tmp.push_back(components.back());
			components.pop_back();
		}

		while(!components.empty() && !tmp.empty()) {
			components.push_back(tmp.back());
			tmp.pop_back();

			get_dir((absolute_path ? "/" : "") + util::join(components, '/'));
		}
	}

	//Write the file.
	std::ofstream file(fname.c_str(),std::ios_base::binary);
	file << data;
}

void move_file(const std::string& from, const std::string& to)
{
	rename(from.c_str(), to.c_str());
}

void remove_file(const std::string& fname)
{
	unlink(fname.c_str());
}

void copy_file(const std::string& from, const std::string& to)
{
#ifdef _WINDOWS
	ASSERT_LOG(CopyFileA(from.c_str(), to.c_str(), false), "copy_file: (" << from << " : " << to << ") failed.");
#else
	// Note that this is a pretty gross copy operation and won't preserve meta-data
	// it will only copy the file data.  If your API has a better copy option, I'd
	// suggest implementing it here.
	const std::string contents = sys::read_file(from);
	sys::write_file(to, contents);
#endif
}

void rmdir_recursive(const std::string& path)
{
	std::vector<std::string> files, dirs;
	sys::get_files_in_dir(path, &files, &dirs, sys::ENTIRE_FILE_PATH);
	foreach(const std::string& file, files) {
		sys::remove_file(file);
	}

	foreach(const std::string& dir, dirs) {
		rmdir_recursive(dir);
	}

	rmdir(path.c_str());
}

// Take a path and convert to the conforming definition, back-slashes converted to forward slashes 
// and no trailing slash.
std::string make_conformal_path(const std::string& path) 
{
	std::string new_path(path);
	std::replace(new_path.begin(), new_path.end(),'\\','/');
	new_path = boost::regex_replace(new_path, boost::regex("//"), "/",  boost::match_default | boost::format_all);
	if(new_path[new_path.length()-1] == '/') {
		new_path.erase(new_path.length()-1);
	}
	return new_path;
}

std::string del_substring_front(const std::string& target, const std::string& common)
{
	if(boost::iequals(target.substr(0, common.length()), common)) {
	//if(target.find(common) == 0) {
		return target.substr(common.length());
	}
	return target;
}

std::string normalise_path(const std::string& path)
{
	if(is_path_absolute(path)) { 
		return path;
	}
	std::vector<std::string> cur_path;
	std::string norm_path;
	boost::split(cur_path, path, std::bind2nd(std::equal_to<char>(), '/'));
	foreach(const std::string& s, cur_path) {
		if(s != ".") {
			norm_path += s + "/";
		}
	}
	return norm_path;
}

// Calculates the path of target relative to source.
std::string compute_relative_path(const std::string& source, const std::string& target)
{
	//std::cerr << "compute_relative_path(a): " << source << " : " << target << std::endl;
	std::string common_part = normalise_path(source);
	std::string back;
	if(common_part.length() > 1 && common_part[common_part.length()-1] == '/') {
		common_part.erase(common_part.length()-1);
	}
	while(boost::iequals(del_substring_front(target, common_part), target)) {
		size_t offs = common_part.rfind('/');
		//std::cerr << "compute_relative_path(b2): " << back << " : " << common_part << std::endl;
		if(common_part.length() > 1 && offs != std::string::npos) {
			common_part.erase(offs);
			back = "../" + back;
		} else {
			break;
		}
	}
	common_part = del_substring_front(target, common_part);
	if(common_part.length() == 1) {
		common_part = common_part.substr(1);
		if(back.empty() == false) {
			back.erase(back.length()-1);
		}
	} else if(common_part.length() > 1 && common_part[0] == '/') {
		common_part = common_part.substr(1);
	} else {
		if(back.empty() == false) {
			back.erase(back.length()-1);
		}
	}
	//std::cerr << "compute_relative_path(b): " << back << " : " << common_part << std::endl;
	return back + common_part;
}

bool is_path_absolute(const std::string& path)
{
	static const std::string re_absolute_path = "^(?:(?:(?:[A-Za-z]:)?(?:\\\\|/))|\\\\\\\\|/).*";
	return boost::regex_match(path, boost::regex(re_absolute_path));
}

namespace {
typedef std::map<std::string, std::vector<boost::function<void()> > > file_mod_handler_map;
file_mod_handler_map& get_mod_map() {
	static file_mod_handler_map instance;
	return instance;
}

std::vector<std::string> new_files_listening;

threading::mutex& get_mod_map_mutex() {
	static threading::mutex instance;
	return instance;
}

std::vector<boost::function<void()> > file_mod_notification_queue;

threading::mutex& get_mod_queue_mutex() {
	static threading::mutex instance;
	return instance;
}

void file_mod_worker_thread_fn()
{
#ifdef __linux__
	const int inotify_fd = inotify_init();
	std::map<int, std::string> fd_to_path;
	fd_set read_set;
#endif

	std::map<std::string, int64_t> mod_times;
	for(;;) {
		file_mod_handler_map m;
		std::vector<std::string> new_files;

		{
			threading::lock lck(get_mod_map_mutex());
			m = get_mod_map();
			new_files = new_files_listening;
			new_files_listening.clear();
		}

		if(m.empty()) {
			break;
		}

#ifdef __linux__
		for(int n = 0; n != new_files.size(); ++n) {
			const int fd = inotify_add_watch(inotify_fd, new_files[n].c_str(), IN_CLOSE_WRITE);
			if(fd > 0) {
				fd_to_path[fd] = new_files[n];
			} else {
				std::cerr << "COULD NOT LISTEN ON FILE " << new_files[n] << "\n";
			}
		}

		FD_ZERO(&read_set);
		FD_SET(inotify_fd, &read_set);
		timeval tv = {1, 0};
		const int select_res = select(inotify_fd+1, &read_set, NULL, NULL, &tv);
		if(select_res > 0) {
			inotify_event ev;
			const int nbytes = read(inotify_fd, &ev, sizeof(ev));
			if(nbytes == sizeof(ev)) {

				const std::string path = fd_to_path[ev.wd];
				std::cerr << "LINUX FILE MOD: " << path << "\n";
				if(ev.mask&IN_IGNORED) {
					fd_to_path.erase(ev.wd);
					const int fd = inotify_add_watch(inotify_fd, path.c_str(), IN_MODIFY);
					if(fd > 0) {
						fd_to_path[fd] = path;
					}
				}
				std::vector<boost::function<void()> >& handlers = m[path];
				std::cerr << "FILE HANDLERS: " << handlers.size() << "\n";

				threading::lock lck(get_mod_queue_mutex());
				file_mod_notification_queue.insert(file_mod_notification_queue.end(), handlers.begin(), handlers.end());
			} else {
				std::cerr << "READ FAILURE IN FILE NOTIFY\n";
			}
		}

#else

		const int begin = SDL_GetTicks();

		for(file_mod_handler_map::iterator i = m.begin(); i != m.end(); ++i) {
			std::map<std::string, int64_t>::iterator mod_itor = mod_times.find(i->first);
			const int64_t mod_time = file_mod_time(i->first);
			if(mod_itor == mod_times.end()) {
				mod_times[i->first] = mod_time;
			} else if(mod_time != mod_itor->second) {
				std::cerr << "MODIFY: " << mod_itor->first << "\n";
				mod_itor->second = mod_time;

				threading::lock lck(get_mod_queue_mutex());
				file_mod_notification_queue.insert(file_mod_notification_queue.end(), i->second.begin(), i->second.end());
			}
		}

		//std::cerr << "CHECKED " << m.size() << " FILES IN " << (SDL_GetTicks() - begin) << "\n";

		SDL_Delay(100);
#endif
	}
}

threading::thread* file_mod_worker_thread = NULL;

}

void notify_on_file_modification(const std::string& path, boost::function<void()> handler)
{
	{
		threading::lock lck(get_mod_map_mutex());
		std::vector<boost::function<void()> >& handlers = get_mod_map()[path];
		if(handlers.empty()) {
			new_files_listening.push_back(path);
		}
		handlers.push_back(handler);
	}

	if(file_mod_worker_thread == NULL) {
		file_mod_worker_thread = new threading::thread(file_mod_worker_thread_fn);
	}
}

void pump_file_modifications()
{
	if(file_mod_worker_thread == NULL) {
		return;
	}

	std::vector<boost::function<void()> > v;
	{
		threading::lock lck(get_mod_queue_mutex());
		v.swap(file_mod_notification_queue);
	}

	foreach(boost::function<void()> f, v) {
		std::cerr << "CALLING FILE MOD HANDLER\n";
		f();
	}
}

filesystem_manager::filesystem_manager()
{
}

filesystem_manager::~filesystem_manager()
{
	{
		threading::lock lck(get_mod_map_mutex());
		get_mod_map().clear();
	}

	delete file_mod_worker_thread;
	file_mod_worker_thread = NULL;
}

}


UNIT_TEST(absolute_path_test1) {
	CHECK_EQ(sys::is_path_absolute("images"), false);
	CHECK_EQ(sys::is_path_absolute("images/"), false);
	CHECK_EQ(sys::is_path_absolute("./images"), false);
	CHECK_EQ(sys::is_path_absolute("/home"), true);
	CHECK_EQ(sys::is_path_absolute("/home/worker"), true);
	CHECK_EQ(sys::is_path_absolute("c:\\home"), true);
	CHECK_EQ(sys::is_path_absolute("c:\\"), true);
	CHECK_EQ(sys::is_path_absolute("\\"), true);
	CHECK_EQ(sys::is_path_absolute("\\home"), true);
	CHECK_EQ(sys::is_path_absolute("\\\\.\\"), true);
	CHECK_EQ(sys::is_path_absolute("\\\\unc\\test"), true);
	CHECK_EQ(sys::is_path_absolute("c:/home"), true);
	CHECK_EQ(sys::is_path_absolute("c:/"), true);
}
