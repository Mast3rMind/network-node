/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * util.cpp
*/

#include "CCdef.h"
#include "util.h"

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

ccthreadid_t ccgetthreadid()
{
#ifdef _WIN32
	return GetCurrentThreadId();
#else
#error need to implement get_thread_id
#endif
}

void ccsleep(int sec)
{
	//BOOST_LOG_TRIVIAL(trace) << "ccsleep start " << sec << " g_shutdown " << g_shutdown;

	for (int i = 0; i < sec; ++i)
	{
		if (!g_shutdown)
			usleep(1000*1000-1);	// must be smaller than 1 second
	}

	//BOOST_LOG_TRIVIAL(trace) << "ccsleep end " << sec << " g_shutdown " << g_shutdown;
}

static wstring get_process_dir()
{
#ifdef _WIN32
	wstring path(MAX_PATH, 0);
	while (true)
	{
		unsigned int len = GetModuleFileNameW(NULL, &path[0], static_cast< unsigned int >(path.size()));
		if (len < path.size())
		{
			path.resize(len);
			break;
		}

		if (path.size() > 65536)
			return wstring();

		path.resize(path.size() * 2);
	}

	return boost::filesystem::path(path).parent_path().wstring();

#else

	if (boost::filesystem::exists("/proc/self/exe"))
		return boost::filesystem::read_symlink("/proc/self/exe").parent_path().wstring();

	if (boost::filesystem::exists("/proc/curproc/file"))
		return boost::filesystem::read_symlink("/proc/curproc/file").parent_path().wstring();

	if (boost::filesystem::exists("/proc/curproc/exe"))
		return boost::filesystem::read_symlink("/proc/curproc/exe").parent_path().wstring();

	return wstring();
#endif
}

static wstring get_app_data_dir()
{
	wstring path;

	if (g_params.config_options.count("datadir"))
	{
		path = g_params.config_options.at("datadir").as<wstring>();
		//cerr << "datadir " << w2s(path) << endl;
	}

	if (path.empty())
	{

#ifdef _WIN32

		path.resize(MAX_PATH);
		if (S_OK != SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, &path[0]))
			return wstring();

		path.resize(wcslen(path.c_str()));

		path += WIDE(PATH_DELIMITER);

#else

		const char *home = getenv("HOME");
		if (!home)
			return wstring();

		path = s2w(home);
		path += WIDE(PATH_DELIMITER);
		path += L".";

#endif

		path += L"CredaCash";

		if (create_directory(path))
		{
			BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to create directory " << w2s(path);
			exit(-1);
			throw exception();
			return wstring();
		}

		path += WIDE(PATH_DELIMITER);
		path += L"CCNode";
	}

	if (create_directory(path))
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to create directory " << w2s(path);
		exit(-1);
		throw exception();
		return wstring();
	}

	wstring path2 = path + s2w(TOR_SUBDIR);
	if (create_directory(path2))
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to create directory " << w2s(path2);
		exit(-1);
		throw exception();
		return wstring();
	}

	path2 = path + s2w(TOR_HOSTNAMES_SUBDIR);
	if (create_directory(path2))
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to create directory " << w2s(path2);
		exit(-1);
		throw exception();
		return wstring();
	}

	return path;
}

int init_globals()
{
	g_params.app_data_dir = get_app_data_dir();

	if (g_params.app_data_dir.empty())
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to get application data directory";
		exit(-1);
		throw exception();
		return -1;
	}

	BOOST_LOG_TRIVIAL(debug) << "application data directory = " << w2s(g_params.app_data_dir);

	return 0;
}

int init_app_dir()
{
	g_params.process_dir = get_process_dir();

	if (g_params.process_dir.empty())
	{
		BOOST_LOG_TRIVIAL(fatal) << "FATAL ERROR: Unable to get process directory";
		exit(-1);
		throw exception();
		return -1;
	}

	BOOST_LOG_TRIVIAL(debug) << "process directory = " << w2s(g_params.process_dir);

	return 0;
}

int create_directory(const wstring& path)
{

#ifdef _WIN32

	DWORD fa = GetFileAttributesW(path.c_str());
	if ((fa == INVALID_FILE_ATTRIBUTES || !(fa & FILE_ATTRIBUTE_DIRECTORY))
				&& !CreateDirectoryW(path.c_str(), NULL))
		return -1;

#else

	if (mkdir(w2s(path).c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP) && errno != EEXIST)
		return -1;

#endif

	return 0;
}

int open_file(const wstring& path, int flags, int mode)
{
	//cout << "open_file " << w2s(path) << endl;

#ifdef _WIN32
	int fd = _wopen(path.c_str(), flags, mode);
#else
	int fd = open(w2s(path).c_str(), flags, mode);
#endif

	return fd;
}

int delete_file(const wstring& path)
{
#ifdef _WIN32
	return _wunlink(path.c_str());
#else
	return unlink(w2s(path).c_str());
#endif
}
