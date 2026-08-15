/*
 * core/src/nes_stream.hpp — 内存字节流适配器 (t2b §4)
 *
 * NestopiaUE 的所有 IO (Machine::Load / LoadState / SaveState / Fds::SetBIOS / Patch)
 * 都走 std::istream / std::ostream, 而 C ABI 面是「字节指针 + 长度」。
 * 本文件提供两类适配器把两者桥接起来:
 *
 *   MemIStream  — 只读输入流, 包住 [data, data+size)。
 *   MemOStream  — 只写输出流, 带容量上限检查 (save state 用)。
 *
 * 约定 (t2b §4.4/§4.6):
 *   - 适配器与流对象必须在调用 Machine::Load/SaveState 的同一个栈帧内活过全程;
 *   - 不抛自定义异常, 超容用「截断 + badbit」表达 (Nestopia 会把 bad_alloc 捕获为
 *     RESULT_ERR_OUT_OF_MEMORY, 把坏流写抛为 RESULT_ERR_CORRUPT_FILE);
 *   - 非阻塞、不得重入 nes_*。
 *
 * 注意: t2b §4.2 曾断言「顺序读、不需要 seek」, 但真实源码
 * (NstCartridgeInes.cpp / NstCartridgeUnif.cpp / NstCartridgeRomset.cpp) 在
 * iNES/UNIF 加载时会调用 stream.Seek()/stream.Length() (即 seekg/tellg),
 * 因此 MemIStream 必须实现 seekoff/seekpos, 否则带 trainer 的 iNES 或
 * romset 校验会以 RESULT_ERR_CORRUPT_FILE 失败。
 *
 * 命名: 不用 namespace nes —— nes.h 的 C 句柄标签 `struct nes` 与之冲突。
 */
#ifndef NES_STREAM_HPP
#define NES_STREAM_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <streambuf>

namespace nes_stream
{
	// ------------------------------------------------------------------
	// 只读内存输入流
	// ------------------------------------------------------------------
	class MemIStream
	{
	public:
		MemIStream(const uint8_t* data, size_t size);
		std::istream& stream() { return in_; }

	private:
		struct Buf : std::streambuf
		{
			Buf(const uint8_t* data, size_t size);
			int_type underflow() override;
			pos_type seekoff(off_type off, std::ios_base::seekdir dir,
			                 std::ios_base::openmode which) override;
			pos_type seekpos(pos_type pos, std::ios_base::openmode which) override;
		};

		Buf buf_;
		std::istream in_;
	};

	// ------------------------------------------------------------------
	// 只写内存输出流 (save state 等), 带容量上限:
	//   - 超容时置 overflowed 标志, xsputn 返回 0 / overflow 返回 eof,
	//     使 std::ostream 进入 badbit (Nestopia 的 Stream::Out::Write
	//     会因此抛 RESULT_ERR_CORRUPT_FILE, 壳层据此映射 NES_ERR_BUFFER_TOO_SMALL);
	//   - *written 累计实际写入字节数, *needed 累计真实需求(写入尝试总量)。
	// ------------------------------------------------------------------
	class MemOStream
	{
	public:
		MemOStream(uint8_t* buf, size_t cap, size_t* written, size_t* needed);
		std::ostream& stream() { return out_; }
		bool overflowed() const { return overflowed_; }

	private:
		struct Buf : std::streambuf
		{
			Buf(uint8_t* buf, size_t cap, size_t* written, size_t* needed, bool* overflowed);
			int_type overflow(int_type c) override;
			std::streamsize xsputn(const char* s, std::streamsize n) override;

			uint8_t* buf_;
			size_t cap_;
			size_t* written_;
			size_t* needed_;
			bool* overflowed_;
		};

		Buf buf_;
		std::ostream out_;
		bool overflowed_;
	};
}

#endif /* NES_STREAM_HPP */