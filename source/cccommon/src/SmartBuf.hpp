/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2016 Creda Software, Inc.
 *
 * SmartBuf.hpp
*/

#pragma once

#include <cstdlib>
#include <cstdint>
#include <atomic>
#include <iostream>

#define TRACE_SMARTBUF		0

#define USE_SMARTBUF_GUARD	1

#define SMARTBUF_GUARD	0x84758362
#define SMARTBUF_FREE	0x28472919

class SmartBuf
{
	typedef std::atomic<std::uint32_t> refcount_t;
	typedef volatile std::uint32_t nauxptrs_t;	// volatile in case a SmartBuf instance is accessed from more than one thread

	std::atomic<std::uint8_t*> buf;				// atomic in case a SmartBuf instance is accessed from more than one thread

	std::size_t alloc_size() const;

public:

#if TRACE_SMARTBUF
	SmartBuf();
#else
	SmartBuf()
		: buf(NULL)
	{ }
#endif

	SmartBuf(std::size_t bufsize);

	void CheckGuard(bool refcount_iszero = false) const;

	std::size_t size() const;

	std::uint8_t* data(int refcount_iszero = false) const;

	void SetAuxPtrCount(unsigned count);

	unsigned GetAuxPtrCount() const;

	void SetRefCount(unsigned count);

	unsigned GetRefCount() const;

	unsigned IncRef();

	unsigned DecRef();

	~SmartBuf();

	SmartBuf(void* p);

	SmartBuf(const SmartBuf& s);

	SmartBuf& operator= (const SmartBuf& s);

	SmartBuf(SmartBuf&& s);

	SmartBuf& operator= (SmartBuf&& s);

	void ClearRef();

	void SetBasePtr(void* p);

	void* BasePtr() const;

	operator bool() const;

	bool operator== (const SmartBuf &s) const
	{
		return BasePtr() == s.BasePtr();
	}

	bool operator!= (const SmartBuf &s) const
	{
		return !(*this == s);
	}
};
