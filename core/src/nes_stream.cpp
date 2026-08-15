/*
 * core/src/nes_stream.cpp — 内存字节流适配器实现 (t2b §4)
 */
#include "nes_stream.hpp"

#include <cstring>
#include <istream>
#include <ostream>

namespace nes_stream
{
	// ------------------------------------------------------------------
	// MemIStream
	// ------------------------------------------------------------------

	MemIStream::Buf::Buf(const uint8_t* data, size_t size)
	{
		char* begin = const_cast<char*>(reinterpret_cast<const char*>(data));
		setg(begin, begin, begin + size);
	}

	MemIStream::Buf::int_type MemIStream::Buf::underflow()
	{
		return gptr() == egptr()
			? traits_type::eof()
			: traits_type::to_int_type(*gptr());
	}

	MemIStream::Buf::pos_type MemIStream::Buf::seekoff(off_type off,
	                                                   std::ios_base::seekdir dir,
	                                                   std::ios_base::openmode which)
	{
		if (!(which & std::ios_base::in))
			return pos_type(off_type(-1));

		char* const base = eback();
		char* const end  = egptr();
		char* const cur  = gptr();

		off_type pos;
		switch (dir)
		{
			case std::ios_base::beg: pos = off; break;
			case std::ios_base::cur: pos = off + (cur - base); break;
			case std::ios_base::end: pos = off + (end - base); break;
			default: return pos_type(off_type(-1));
		}

		if (pos < 0 || pos > (end - base))
			return pos_type(off_type(-1));

		setg(base, base + pos, end);
		return pos_type(pos);
	}

	MemIStream::Buf::pos_type MemIStream::Buf::seekpos(pos_type pos,
	                                                   std::ios_base::openmode which)
	{
		return seekoff(off_type(pos), std::ios_base::beg, which);
	}

	MemIStream::MemIStream(const uint8_t* data, size_t size)
		: buf_(data, size), in_(&buf_)
	{
	}

	// ------------------------------------------------------------------
	// MemOStream
	// ------------------------------------------------------------------

	MemOStream::Buf::Buf(uint8_t* buf, size_t cap, size_t* written, size_t* needed, bool* overflowed)
		: buf_(buf), cap_(cap), written_(written), needed_(needed), overflowed_(overflowed)
	{
	}

	MemOStream::Buf::int_type MemOStream::Buf::overflow(int_type c)
	{
		if (c == traits_type::eof())
			return traits_type::eof();

		if (*written_ >= cap_)
		{
			// 超容: 置标志, 返回 eof → ostream 置 badbit
			*overflowed_ = true;
			*needed_ += 1;
			return traits_type::eof();
		}

		buf_[(*written_)++] = traits_type::to_char_type(c);
		*needed_ += 1;
		return c;
	}

	std::streamsize MemOStream::Buf::xsputn(const char* s, std::streamsize n)
	{
		if (n <= 0)
			return 0;

		if (static_cast<size_t>(n) <= cap_ - *written_)
		{
			std::memcpy(buf_ + *written_, s, static_cast<size_t>(n));
			*written_ += static_cast<size_t>(n);
			*needed_ += static_cast<size_t>(n);
			return n;
		}

		// 整块放不下: 不拷贝, 置标志并返回 0 (std::ostream 会置 badbit),
		// *needed 累计真实需求供调用方扩容重试。
		*overflowed_ = true;
		*needed_ += static_cast<size_t>(n);
		return 0;
	}

	MemOStream::MemOStream(uint8_t* buf, size_t cap, size_t* written, size_t* needed)
		: buf_(buf, cap, written, needed, &overflowed_), out_(&buf_), overflowed_(false)
	{
	}
}
