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
	//
	// 游标模型: cur_ = 当前写位置 (seekp 移动它), end_ = 已写最大范围
	// (= 缓冲内有效字节数; 完整存档成功后即最终流长度)。写入只落在
	// [cur_, cur_+n), seek 只改 cur_ —— 状态存档器的 chunk 长度回填
	// (NstState.cpp Saver::End 的 Seek(-len)/Write32/Seek(len)) 因此可用。
	// 超容时置 overflowed_ 并立即中止 (不部分写入): Nestopia 在首次写
	// 失败处抛异常, 之后的 seek/写不会再发生; seek 不清除 overflowed_。
	// ------------------------------------------------------------------

	MemOStream::Buf::Buf(uint8_t* buf, size_t cap, size_t* written, size_t* needed, bool* overflowed)
		: buf_(buf), cap_(cap), cur_(0), end_(0), needed_total_(0),
		  written_(written), needed_(needed), overflowed_(overflowed)
	{
		*written_ = 0;
		*needed_ = 0;
	}

	MemOStream::Buf::int_type MemOStream::Buf::overflow(int_type c)
	{
		if (c == traits_type::eof())
			return traits_type::eof();

		if (cur_ >= cap_)
		{
			// 超容: 置标志, 返回 eof → ostream 置 badbit;
			// 该字节本会扩展流 (cur_ ≥ end_ 恒成立), 计入需求下界。
			*overflowed_ = true;
			needed_total_ += 1;
			*needed_ = needed_total_;
			return traits_type::eof();
		}

		buf_[cur_++] = traits_type::to_char_type(c);
		if (cur_ > end_)
		{
			end_ = cur_;
			needed_total_ = end_;
			*written_ = end_;
			*needed_ = end_;
		}
		return c;
	}

	std::streamsize MemOStream::Buf::xsputn(const char* s, std::streamsize n)
	{
		if (n <= 0)
			return 0;

		if (cur_ + static_cast<size_t>(n) > cap_)
		{
			// 整块放不下: 不拷贝、不部分写入 (Nestopia 在首次写失败处立即
			// 中止), 置标志并返回 0 → std::ostream 置 badbit。需求下界累加
			// 「本会扩展流」的字节数 (cur_+n > end_ 恒成立, 因 end_ ≤ cap_)。
			*overflowed_ = true;
			needed_total_ += cur_ + static_cast<size_t>(n) - end_;
			*needed_ = needed_total_;
			return 0;
		}

		std::memcpy(buf_ + cur_, s, static_cast<size_t>(n));
		cur_ += static_cast<size_t>(n);
		if (cur_ > end_)
		{
			end_ = cur_;
			needed_total_ = end_;
			*written_ = end_;
			*needed_ = end_;
		}
		return n;
	}

	MemOStream::Buf::pos_type MemOStream::Buf::seekoff(off_type off,
	                                                   std::ios_base::seekdir dir,
	                                                   std::ios_base::openmode which)
	{
		// 只接受输出方向 (ostream::seekp); 镜像 MemIStream::seekoff 的风格。
		if (!(which & std::ios_base::out))
			return pos_type(off_type(-1));

		off_type pos;
		switch (dir)
		{
			case std::ios_base::beg: pos = off; break;
			case std::ios_base::cur: pos = off + static_cast<off_type>(cur_); break;
			case std::ios_base::end: pos = off + static_cast<off_type>(end_); break;
			default: return pos_type(off_type(-1));
		}

		// 越界 (负值或超出容量) → -1 (ostream 置 failbit, Nestopia 的
		// Out::Seek 会抛 RESULT_ERR_CORRUPT_FILE)。不触碰 overflowed_。
		if (pos < 0 || pos > static_cast<off_type>(cap_))
			return pos_type(off_type(-1));

		cur_ = static_cast<size_t>(pos);
		return pos_type(pos);
	}

	MemOStream::Buf::pos_type MemOStream::Buf::seekpos(pos_type pos,
	                                                   std::ios_base::openmode which)
	{
		return seekoff(off_type(pos), std::ios_base::beg, which);
	}

	int MemOStream::Buf::sync()
	{
		return 0; // 内存缓冲无待 flush 内容
	}

	MemOStream::MemOStream(uint8_t* buf, size_t cap, size_t* written, size_t* needed)
		: buf_(buf, cap, written, needed, &overflowed_), out_(&buf_), overflowed_(false)
	{
	}

	// ------------------------------------------------------------------
	// GrowableOStream (S1-2)
	//
	// vector 后端、无 cap: overflow/xsputn 在 cur_ 触及末尾时自动扩容;
	// 游标模型同 MemOStream —— cur_ = 写游标, data_.size() = 高水位
	// (= 完整写出后的最终流长度)。seekp 只移动 cur_, 回填写入原地覆盖
	// [cur_, cur_+n), 供状态存档器的 chunk 长度回填使用。
	// ------------------------------------------------------------------

	GrowableOStream::Buf::Buf(std::vector<uint8_t>* data)
		: data_(data), cur_(0)
	{
	}

	GrowableOStream::Buf::int_type GrowableOStream::Buf::overflow(int_type c)
	{
		if (c == traits_type::eof())
			return traits_type::eof();

		if (cur_ >= data_->size())
			data_->resize(cur_ + 1);
		(*data_)[cur_++] = traits_type::to_char_type(c);
		return c;
	}

	std::streamsize GrowableOStream::Buf::xsputn(const char* s, std::streamsize n)
	{
		if (n <= 0)
			return 0;

		if (cur_ + static_cast<size_t>(n) > data_->size())
			data_->resize(cur_ + static_cast<size_t>(n));
		std::memcpy(data_->data() + cur_, s, static_cast<size_t>(n));
		cur_ += static_cast<size_t>(n);
		return n;
	}

	GrowableOStream::Buf::pos_type GrowableOStream::Buf::seekoff(off_type off,
	                                                             std::ios_base::seekdir dir,
	                                                             std::ios_base::openmode which)
	{
		if (!(which & std::ios_base::out))
			return pos_type(off_type(-1));

		off_type pos;
		switch (dir)
		{
			case std::ios_base::beg: pos = off; break;
			case std::ios_base::cur: pos = off + static_cast<off_type>(cur_); break;
			case std::ios_base::end: pos = off + static_cast<off_type>(data_->size()); break;
			default: return pos_type(off_type(-1));
		}

		if (pos < 0 || pos > static_cast<off_type>(data_->size()))
			return pos_type(off_type(-1));

		cur_ = static_cast<size_t>(pos);
		return pos_type(pos);
	}

	GrowableOStream::Buf::pos_type GrowableOStream::Buf::seekpos(pos_type pos,
	                                                             std::ios_base::openmode which)
	{
		return seekoff(off_type(pos), std::ios_base::beg, which);
	}

	int GrowableOStream::Buf::sync()
	{
		return 0; // 内存缓冲无待 flush 内容
	}

	GrowableOStream::GrowableOStream()
		: buf_(&data_), out_(&buf_)
	{
	}
}
